#include "textboardwidget.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>

#include <utility>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kPadding = 12;
// Reserved at the top of the cell for the fragment-count/jitter info strip,
// drawn only above a gray matrix (onBinarySample()'s decoded camera.matrix
// path) -- the plain text/legacy binary-matrix paths carry neither metric.
constexpr int kInfoStripHeight = 18;
// Low enough that even a many-line report still fits (shrinks to unreadable
// before it clips) in a small cell -- the board's contract is "always show
// the whole document", the operator resizes the cell to make it legible.
constexpr int kMinFontPixelSize = 3;
constexpr int kMaxFontPixelSize = 72;

quint32 parseSourceId(const QJsonObject& json) {
    return quint32(json.value("sourceId").toString("0").toULongLong(nullptr, 0));
}

quint16 parseTopicId(const QJsonObject& json) {
    return quint16(qBound(0, json.value("topicId").toString("0").toInt(nullptr, 0), 65535));
}

QFont boardFont(int pixelSize) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    font.setPixelSize(pixelSize);
    return font;
}

bool textFits(const QStringList& lines, const QSize& available, int pixelSize) {
    if (available.width() <= 0 || available.height() <= 0) {
        return false;
    }
    const QFontMetricsF metrics(boardFont(pixelSize));
    if (metrics.lineSpacing() * lines.size() > available.height()) {
        return false;
    }
    for (const QString& line : lines) {
        if (metrics.horizontalAdvance(line) > available.width()) {
            return false;
        }
    }
    return true;
}

QString binaryBitsInLine(const QString& line) {
    QString bits;
    bits.reserve(line.size());
    for (const QChar character : line) {
        if (character == QLatin1Char('0') || character == QLatin1Char('1')) {
            bits.append(character);
        } else if (!character.isSpace()) {
            return {};
        }
    }
    return bits;
}

// A grid decoded from an OPAQUE_BYTES camera.matrix sample, every cell
// already scaled to an 8-bit grayscale level (0-255) regardless of the wire
// bits_per_pixel it arrived at.
struct DecodedMatrix {
    bool ok = false;
    int rows = 0;
    int cols = 0;
    QVector<quint8> samples;  // row-major, rows*cols entries
};

// Reverses Camera.cpp's build_matrix (bally_dongle esp32-cam_project):
// v1: body[0]=rows, body[1]=cols, body[2]=bits_per_pixel, then
// rows*ceil(cols*bits_per_pixel/8) bytes of packed samples, one row after
// another, each row starting its own byte (never spanning a byte boundary),
// samples packed MSB-first within a row (the first/leftmost column occupies
// the top bits_per_pixel bits of the row's first byte). Each sample is
// widened to 0-255 by scaling against its bits_per_pixel's own max value, so
// bits_per_pixel==1 yields exactly 0 or 255 (pure black/white) and higher
// depths yield intermediate grays. Returns !ok for a body that doesn't fit
// that shape (too short, an unsupported bits_per_pixel, or a size
// inconsistent with its own header) instead of guessing at a partial image.
// v2 adds body[3]=encoding (0=RAW, 1=RLE). Depth<8 uses high depth bits
// for value and low bits for count-1; depth=8 uses {value, count-1} bytes.
DecodedMatrix decodeMatrix(const QByteArray& body, quint16 schemaVersion) {
    DecodedMatrix result;
    const int headerSize = schemaVersion == 1 ? 3 : 4;
    if ((schemaVersion != 1 && schemaVersion != 2) || body.size() < headerSize) {
        return result;
    }
    const int rows = static_cast<unsigned char>(body.at(0));
    const int cols = static_cast<unsigned char>(body.at(1));
    const int bitsPerPixel = static_cast<unsigned char>(body.at(2));
    if (rows <= 0 || cols <= 0) {
        return result;
    }
    if (bitsPerPixel != 1 && bitsPerPixel != 2 && bitsPerPixel != 4 && bitsPerPixel != 8) {
        return result;
    }

    const int rowBytes = (cols * bitsPerPixel + 7) / 8;
    const qsizetype expectedSize = headerSize + qsizetype(rowBytes) * qsizetype(rows);
    const unsigned encoding = schemaVersion == 1 ? 0 : static_cast<unsigned char>(body.at(3));
    if (encoding > 1 || (encoding == 0 && body.size() != expectedSize)) return result;

    const int samplesPerByte = 8 / bitsPerPixel;
    const int maxSample = (1 << bitsPerPixel) - 1;
    const auto* packed = reinterpret_cast<const unsigned char*>(body.constData()) + headerSize;

    QVector<quint8> samples;
    samples.reserve(rows * cols);
    const auto levelOf = [bitsPerPixel, maxSample](int value) -> quint8 {
        // Preserve the legacy binary display convention (set bit = black).
        return bitsPerPixel == 1 ? (value != 0 ? 0 : 255)
                                : static_cast<quint8>(qRound(value * 255.0 / maxSample));
    };
    if (encoding == 0) {
        for (int row = 0; row < rows; ++row) {
            const unsigned char* rowBits = packed + row * rowBytes;
            for (int column = 0; column < cols; ++column) {
                const int shift = (samplesPerByte - 1 - column % samplesPerByte) * bitsPerPixel;
                samples.append(levelOf((rowBits[column / samplesPerByte] >> shift) & maxSample));
            }
        }
    } else {
        const unsigned countBits = bitsPerPixel == 8 ? 8 : 8 - bitsPerPixel;
        const unsigned countMask = (1U << countBits) - 1U;
        const qsizetype pixelCount = qsizetype(rows) * cols;
        for (qsizetype offset = headerSize; offset < body.size();) {
            const unsigned record = static_cast<unsigned char>(body.at(offset++));
            unsigned value, run;
            if (bitsPerPixel == 8) {
                if (offset == body.size()) return result;
                value = record;
                run = static_cast<unsigned char>(body.at(offset++)) + 1U;
            } else {
                value = record >> countBits;
                run = (record & countMask) + 1U;
            }
            if (run > unsigned(pixelCount - samples.size())) return result;
            const quint8 level = levelOf(value);
            for (unsigned i = 0; i < run; ++i) samples.append(level);
        }
        if (samples.size() != pixelCount) return result;
    }

    result.ok = true;
    result.rows = rows;
    result.cols = cols;
    result.samples = std::move(samples);
    return result;
}

// `bounds` is the area the matrix is centred within -- the whole widget for
// the plain text/legacy binary-matrix paths, or the area left below the info
// strip for a decoded gray matrix (see paintEvent()).
QRectF matrixRect(int rowCount, int columnCount, const QRectF& bounds) {
    const qreal availableWidth = qMax(0.0, bounds.width() - 2 * kPadding);
    const qreal availableHeight = qMax(0.0, bounds.height() - 2 * kPadding);
    const qreal pixelSize = qMin(availableWidth / columnCount, availableHeight / rowCount);
    const QSizeF matrixSize(columnCount * pixelSize, rowCount * pixelSize);
    return QRectF(bounds.x() + (bounds.width() - matrixSize.width()) / 2.0,
                  bounds.y() + (bounds.height() - matrixSize.height()) / 2.0,
                  matrixSize.width(), matrixSize.height());
}

void drawBinaryMatrix(QPainter& painter, const QStringList& rows, const QRectF& bounds) {
    const int rowCount = rows.size();
    const int columnCount = rows.first().size();
    const QRectF matrix = matrixRect(rowCount, columnCount, bounds);
    if (matrix.isEmpty()) {
        return;
    }

    const qreal cellWidth = matrix.width() / columnCount;
    const qreal cellHeight = matrix.height() / rowCount;

    // Start with the zero colour in one pass. Drawing cells from accumulated
    // boundaries below avoids gaps from rounding when the cells are fractional
    // pixels wide.
    painter.fillRect(matrix, Qt::white);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int row = 0; row < rowCount; ++row) {
        for (int column = 0; column < columnCount; ++column) {
            if (rows.at(row).at(column) != QLatin1Char('1')) {
                continue;
            }
            const qreal left = matrix.left() + column * cellWidth;
            const qreal top = matrix.top() + row * cellHeight;
            const qreal right = matrix.left() + (column + 1) * cellWidth;
            const qreal bottom = matrix.top() + (row + 1) * cellHeight;
            painter.drawRect(QRectF(left, top, right - left, bottom - top));
        }
    }
}

// Same raster layout as drawBinaryMatrix, but each cell gets its own
// grayscale fill instead of a shared black/white brush -- used for decoded
// OPAQUE_BYTES samples (see decodeMatrix()), which carry a per-cell level
// rather than a '0'/'1' character.
void drawGrayMatrix(QPainter& painter, int rowCount, int columnCount,
                    const QVector<quint8>& samples, const QRectF& bounds) {
    const QRectF matrix = matrixRect(rowCount, columnCount, bounds);
    if (matrix.isEmpty()) {
        return;
    }

    const qreal cellWidth = matrix.width() / columnCount;
    const qreal cellHeight = matrix.height() / rowCount;

    painter.setPen(Qt::NoPen);
    for (int row = 0; row < rowCount; ++row) {
        for (int column = 0; column < columnCount; ++column) {
            const quint8 level = samples.at(row * columnCount + column);
            painter.setBrush(QColor(level, level, level));
            const qreal left = matrix.left() + column * cellWidth;
            const qreal top = matrix.top() + row * cellHeight;
            const qreal right = matrix.left() + (column + 1) * cellWidth;
            const qreal bottom = matrix.top() + (row + 1) * cellHeight;
            painter.drawRect(QRectF(left, top, right - left, bottom - top));
        }
    }
}

// Text for the strip drawn above a decoded gray matrix (see paintEvent()).
// Jitter is relative arrival-vs-producer-clock drift between consecutive
// samples, NOT a capture-to-display latency (see onBinarySample()'s
// comment for why an absolute latency can't be derived here) -- "--" until
// a second sample gives it a baseline to compare against.
QString formatInfoStrip(quint8 fragmentCount, bool hasJitter, double jitterMs) {
    QString text = QObject::tr("%1 pacote(s)").arg(fragmentCount);
    if (hasJitter) {
        text += QObject::tr("   jitter %1%2 ms")
                    .arg(jitterMs >= 0.0 ? QStringLiteral("+") : QString())
                    .arg(jitterMs, 0, 'f', 1);
    } else {
        text += QObject::tr("   jitter --");
    }
    return text;
}

}  // namespace

TextBoardConfig parseTextBoardConfig(const QJsonObject& json) {
    TextBoardConfig config;
    config.sourceId = parseSourceId(json);
    config.topicId = parseTopicId(json);
    config.sampleTimeMs = qMax(1.0, json.value("sampleTimeMs").toDouble(3000.0));
    config.initialText = json.value("text").toString();
    return config;
}

TextBoardWidget::TextBoardWidget(QWidget* parent) : DashboardWidget(parent) {
    setMinimumSize(80, 48);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

void TextBoardWidget::setConfig(const QJsonObject& config) {
    const TextBoardConfig next = parseTextBoardConfig(config);
    const bool bindingChanged = next.sourceId != m_config.sourceId || next.topicId != m_config.topicId;
    m_config = next;
    if (bindingChanged) {
        m_hasLiveText = false;
    }
    // A properties-panel refresh must not replace a live report with the
    // configured waiting text. It is only the content shown before the first
    // matching sample (and after constructing/loading a fresh widget).
    if (!m_hasLiveText) {
        m_text = m_config.initialText;
    }
    if (bindingChanged) {
        m_hasGrayMatrix = false;
    }
    update();
}

void TextBoardWidget::setText(const QString& text) {
    if (m_text == text && m_hasLiveText && !m_hasGrayMatrix) {
        return;
    }
    m_text = text;
    m_hasLiveText = true;
    m_hasGrayMatrix = false;
    update();
}

void TextBoardWidget::appendText(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    m_text += text;
    m_hasLiveText = true;
    m_hasGrayMatrix = false;
    update();
}

void TextBoardWidget::clearText() {
    if (m_text.isEmpty() && m_hasLiveText && !m_hasGrayMatrix) {
        return;
    }
    m_text.clear();
    m_hasLiveText = true;
    m_hasGrayMatrix = false;
    update();
}

void TextBoardWidget::setGrayMatrix(int rows, int cols, QVector<quint8> samples) {
    m_grayRows = rows;
    m_grayCols = cols;
    m_graySamples = std::move(samples);
    m_hasGrayMatrix = true;
    update();
}

void TextBoardWidget::onTextSample(quint32 sourceId, quint16 topicId,
                                   quint64 /*timestampUs*/, const QString& text) {
    if (sourceId != m_config.sourceId || topicId != m_config.topicId) {
        return;
    }
    setText(text);
}

void TextBoardWidget::onBinarySample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                                     quint8 fragmentCount, const QByteArray& body, quint16 schemaVersion) {
    if (sourceId != m_config.sourceId || topicId != m_config.topicId) {
        return;
    }
    const DecodedMatrix decoded = decodeMatrix(body, schemaVersion);
    if (!decoded.ok) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // timestampUs going backwards is the only locally-observable sign the
    // producer rebooted (its monotonic clock restarted near zero, see the
    // header comment) -- treat it the same as "no baseline yet" rather than
    // emitting a nonsensical jitter value across the discontinuity.
    if (m_hasJitterBaseline && timestampUs > m_lastSampleTimestampUs) {
        const double producerDeltaMs = double(timestampUs - m_lastSampleTimestampUs) / 1000.0;
        const double arrivalDeltaMs = double(nowMs - m_lastSampleArrivalMs);
        m_lastJitterMs = arrivalDeltaMs - producerDeltaMs;
        m_hasJitter = true;
    } else {
        m_hasJitter = false;
    }
    m_lastSampleTimestampUs = timestampUs;
    m_lastSampleArrivalMs = nowMs;
    m_hasJitterBaseline = true;
    m_lastFragmentCount = fragmentCount;

    setGrayMatrix(decoded.rows, decoded.cols, decoded.samples);
}

QStringList TextBoardWidget::layoutLines() const {
    if (m_text.isEmpty()) {
        return QStringList{QString()};
    }
    QString body = m_text;
    if (body.endsWith(QLatin1Char('\n'))) {
        body.chop(1);
    }
    return body.split(QLatin1Char('\n'));
}

QStringList TextBoardWidget::binaryMatrixRows() const {
    QStringList best;
    QStringList current;
    int currentWidth = 0;

    const auto finishCurrent = [&best, &current, &currentWidth] {
        if (current.size() > best.size()) {
            best = current;
        }
        current.clear();
        currentWidth = 0;
    };

    for (const QString& line : layoutLines()) {
        const QString bits = binaryBitsInLine(line);
        if (bits.isEmpty()) {
            finishCurrent();
            continue;
        }
        if (!current.isEmpty() && bits.size() != currentWidth) {
            finishCurrent();
        }
        if (current.isEmpty()) {
            currentWidth = bits.size();
        }
        current.append(bits);
    }
    finishCurrent();

    // One isolated 0/1 value is ordinary text, not an image. Requiring two
    // dimensions also keeps common numeric diagnostics in text mode.
    if (best.size() < 2 || best.first().size() < 2) {
        return {};
    }
    return best;
}

int TextBoardWidget::fittedFontPixelSize() const {
    if (m_fittedPixelSize > 0 && m_fittedForText == m_text && m_fittedForSize == size()) {
        return m_fittedPixelSize;
    }

    const QSize available(qMax(0, width() - 2 * kPadding), qMax(0, height() - 2 * kPadding));
    const QStringList lines = layoutLines();

    int low = kMinFontPixelSize;
    int high = kMaxFontPixelSize;
    int best = kMinFontPixelSize;
    while (low <= high) {
        const int candidate = low + (high - low) / 2;
        if (textFits(lines, available, candidate)) {
            best = candidate;
            low = candidate + 1;
        } else {
            high = candidate - 1;
        }
    }

    m_fittedForText = m_text;
    m_fittedForSize = size();
    m_fittedPixelSize = best;
    return best;
}

void TextBoardWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    painter.fillPath(contentFillPath(), palette.surface);
    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(roundedPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5)));

    if (m_hasGrayMatrix) {
        const QRectF stripRect(0, 0, width(), kInfoStripHeight);
        QFont infoFont = painter.font();
        infoFont.setPixelSize(qMin(11, kInfoStripHeight - 6));
        painter.setFont(infoFont);
        painter.setPen(palette.textSecondary);
        painter.drawText(stripRect.adjusted(kPadding, 0, -kPadding, 0),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         formatInfoStrip(m_lastFragmentCount, m_hasJitter, m_lastJitterMs));

        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRectF matrixBounds(0, kInfoStripHeight, width(), height() - kInfoStripHeight);
        drawGrayMatrix(painter, m_grayRows, m_grayCols, m_graySamples, matrixBounds);
        return;
    }

    const QStringList matrixRows = binaryMatrixRows();
    if (!matrixRows.isEmpty()) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        drawBinaryMatrix(painter, matrixRows, QRectF(QPointF(0, 0), size()));
        return;
    }

    if (m_text.isEmpty()) {
        return;
    }

    painter.setFont(boardFont(fittedFontPixelSize()));
    painter.setPen(palette.textPrimary);

    const QFontMetricsF metrics(painter.font());
    const QStringList lines = layoutLines();
    // The whole document is drawn as one block centred in the cell; lines are
    // left-aligned to the block's left edge so the fixed-pitch columns stay
    // aligned instead of each row wobbling to its own centre.
    const QRectF block = textBlockRect();
    qreal baseline = block.top() + metrics.ascent();
    for (const QString& line : lines) {
        painter.drawText(QPointF(block.left(), baseline), line);
        baseline += metrics.lineSpacing();
        // fittedFontPixelSize() already guarantees the whole document fits;
        // this stop only matters in a cell too small for even kMinFontPixelSize,
        // where drawing past the bottom edge is the graceful failure.
        if (baseline - metrics.ascent() > height() - kPadding) {
            break;
        }
    }
}

QRectF TextBoardWidget::textBlockRect() const {
    const QStringList lines = layoutLines();
    const QFontMetricsF metrics(boardFont(fittedFontPixelSize()));

    qreal blockWidth = 0.0;
    for (const QString& line : lines) {
        blockWidth = qMax(blockWidth, metrics.horizontalAdvance(line));
    }
    const qreal blockHeight = metrics.lineSpacing() * lines.size();

    // Centre the block, but never let it start before the padding -- a block
    // larger than the cell (tiny cell, floored font) stays pinned top-left and
    // clips at the far edge rather than spilling off both sides.
    const qreal x = qMax<qreal>(kPadding, (width() - blockWidth) / 2.0);
    const qreal y = qMax<qreal>(kPadding, (height() - blockHeight) / 2.0);
    return QRectF(x, y, blockWidth, blockHeight);
}

}  // namespace traceview

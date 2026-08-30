#include "textboardwidget.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kPadding = 12;
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
    update();
}

void TextBoardWidget::setText(const QString& text) {
    if (m_text == text && m_hasLiveText) {
        return;
    }
    m_text = text;
    m_hasLiveText = true;
    update();
}

void TextBoardWidget::appendText(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    m_text += text;
    m_hasLiveText = true;
    update();
}

void TextBoardWidget::clearText() {
    if (m_text.isEmpty() && m_hasLiveText) {
        return;
    }
    m_text.clear();
    m_hasLiveText = true;
    update();
}

void TextBoardWidget::onTextSample(quint32 sourceId, quint16 topicId,
                                   quint64 /*timestampUs*/, const QString& text) {
    if (sourceId != m_config.sourceId || topicId != m_config.topicId) {
        return;
    }
    setText(text);
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

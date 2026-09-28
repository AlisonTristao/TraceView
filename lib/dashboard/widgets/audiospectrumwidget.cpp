#include "audiospectrumwidget.h"

#include <QFontMetricsF>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "audioplayback.h"
#include "dashboard/widgets/chartpainting.h"
#include "preferences/appsettings.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kRepaintMs = 33;              // ~30 spectra per second
constexpr float kPeakFallDbPerTick = 0.7f;  // ~20 dB/s
constexpr double kMinFrequency = 20.0;
constexpr float kMinDb = -120.0f;
constexpr float kMaxDb = 0.0f;
// dB rows in the Divisions tick placement: one every 20 dB.
constexpr int kDbGridDivisions = 6;
// A block counter jump larger than this is a restarted device, not loss.
constexpr quint32 kMaxGapBlocks = 1000;
// At most this many lost blocks are replayed as silence (the rest would
// only delay the sound).
constexpr quint32 kMaxSilenceBlocks = 8;
constexpr qint64 kPlaybackRetryMs = 5000;

const int kFftSizes[] = {1024, 2048, 4096, 8192, 16384};

quint32 parseHexId(const QJsonObject& json, const char* key) {
    return quint32(
        json.value(QLatin1String(key)).toString(QStringLiteral("0")).toULongLong(nullptr, 0));
}

AudioSpectrumView parseView(const QJsonObject& json) {
    AudioSpectrumView view;
    view.play = json.value(QLatin1String("play")).toBool(true);
    const int fft = json.value(QLatin1String("fft")).toString(QStringLiteral("4096")).toInt();
    view.fftSize = 4096;
    for (int size : kFftSizes) {
        if (size == fft) {
            view.fftSize = size;
        }
    }
    view.logFrequency = json.value(QLatin1String("axis")).toString(QStringLiteral("log")) !=
                        QLatin1String("linear");
    view.peakHold = json.value(QLatin1String("peak")).toBool(true);
    return view;
}

QJsonObject viewToJson(const AudioSpectrumView& view) {
    QJsonObject json;
    json[QLatin1String("play")] = view.play;
    json[QLatin1String("fft")] = QString::number(view.fftSize);
    json[QLatin1String("axis")] =
        view.logFrequency ? QStringLiteral("log") : QStringLiteral("linear");
    json[QLatin1String("peak")] = view.peakHold;
    return json;
}

QString separator() {
    return QStringLiteral("  ·  ");
}

QString frequencyLabel(double hz) {
    if (hz >= 1000.0) {
        const double k = hz / 1000.0;
        return QStringLiteral("%1k").arg(k, 0, 'g', k >= 10.0 ? 3 : 2);
    }
    return QString::number(int(std::lround(hz)));
}

}  // namespace

AudioSpectrumConfig parseAudioSpectrumConfig(const QJsonObject& json) {
    AudioSpectrumConfig config;
    config.sourceId = parseHexId(json, "sourceId");
    config.topicId = quint16(qMin<quint32>(parseHexId(json, "topicId"), 0xFFFF));
    config.seqFieldId = quint16(json.value(QLatin1String("seqFieldId")).toInt(1));
    config.rateFieldId = quint16(json.value(QLatin1String("rateFieldId")).toInt(2));
    config.samplesFieldId = quint16(json.value(QLatin1String("samplesFieldId")).toInt(3));
    config.fullScale = json.value(QLatin1String("fullScale")).toDouble(2048.0);
    if (!(config.fullScale > 0.0)) {
        config.fullScale = 2048.0;
    }
    config.volumePercent = qBound(0, json.value(QLatin1String("volume")).toInt(80), 100);
    return config;
}

AudioSpectrumWidget::AudioSpectrumWidget(QWidget* parent)
    : DashboardWidget(parent), m_history(SpectrumAnalyzer::kMaxSize, 0.0f) {
    setMinimumSize(160, 90);
    m_analyzer.setSize(m_view.fftSize);
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(kRepaintMs);
    connect(m_repaintTimer, &QTimer::timeout, this, [this] {
        if (m_dirty && !m_paused) {
            refreshSpectrum();
            update();
        }
    });
    m_repaintTimer->start();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
    connect(&AppSettings::instance(), &AppSettings::chartStyleChanged, this,
            [this] { update(); });
}

AudioSpectrumWidget::~AudioSpectrumWidget() = default;

void AudioSpectrumWidget::setConfig(const QJsonObject& config) {
    const AudioSpectrumConfig next = parseAudioSpectrumConfig(config);
    const bool rebound = next.sourceId != m_config.sourceId || next.topicId != m_config.topicId;
    m_config = next;
    if (rebound) {
        resetStream();
    }
    setViewConfig(config.value(QLatin1String("view")).toObject());
}

void AudioSpectrumWidget::setViewConfig(const QJsonObject& view) {
    m_view = parseView(view);
    if (m_analyzer.size() != m_view.fftSize) {
        m_analyzer.setSize(m_view.fftSize);
        m_spectrumDb.clear();
        m_peakDb.clear();
        m_dirty = m_historyFill > 0;
    }
    applyPlayback();
    update();
}

QVector<WidgetViewOption> AudioSpectrumWidget::viewOptions() const {
    QVector<WidgetViewOption> options;
    WidgetViewOption play;
    play.id = QStringLiteral("play");
    play.label = tr("Play audio");
    play.value = m_view.play;
    options.append(play);

    WidgetViewOption fft;
    fft.id = QStringLiteral("fft");
    fft.label = tr("FFT size");
    fft.kind = WidgetViewOption::Kind::Choice;
    fft.value = QString::number(m_view.fftSize);
    for (int size : kFftSizes) {
        fft.choices.append({QString::number(size), tr("%1 points").arg(size)});
    }
    fft.startsSection = true;
    options.append(fft);

    WidgetViewOption axis;
    axis.id = QStringLiteral("axis");
    axis.label = tr("Frequency axis");
    axis.kind = WidgetViewOption::Kind::Choice;
    axis.value = m_view.logFrequency ? QStringLiteral("log") : QStringLiteral("linear");
    axis.choices = {{QStringLiteral("log"), tr("Logarithmic")},
                    {QStringLiteral("linear"), tr("Linear")}};
    options.append(axis);

    WidgetViewOption peak;
    peak.id = QStringLiteral("peak");
    peak.label = tr("Peak hold");
    peak.value = m_view.peakHold;
    options.append(peak);
    return options;
}

QJsonObject AudioSpectrumWidget::viewConfigWith(const QString& id, const QVariant& value) const {
    QJsonObject json = viewToJson(m_view);
    if (id == QLatin1String("play") || id == QLatin1String("peak")) {
        json[id] = value.toBool();
    } else if (id == QLatin1String("fft") || id == QLatin1String("axis")) {
        json[id] = value.toString();
    }
    return json;
}

void AudioSpectrumWidget::setPaused(bool paused) {
    m_paused = paused;
    update();
}

void AudioSpectrumWidget::clearChartData() {
    m_lostBlocks = 0;
    m_receivedBlocks = 0;
    m_historyFill = 0;
    m_spectrumDb.clear();
    m_peakDb.clear();
    m_blockDb = SpectrumAnalyzer::kFloorDb;
    update();
}

void AudioSpectrumWidget::resetStream() {
    m_rate = 0.0;
    m_haveSeq = false;
    m_pendingGap = 0;
    clearChartData();
    if (m_playback != nullptr) {
        m_playback->buffer().clear();
    }
}

void AudioSpectrumWidget::applyPlayback() {
    const bool wanted = m_view.play && m_config.topicId != 0;
    if (!wanted) {
        if (m_playback != nullptr && m_playback->isRunning()) {
            m_playback->stop();
        }
        m_playbackNote = m_view.play ? QString() : tr("muted");
        return;
    }
    if (m_playback == nullptr) {
        m_playback = new AudioPlayback(this);
    }
    m_playback->setVolume(std::pow(m_config.volumePercent / 100.0, 2.0));  // perceptual
    if (m_playback->isRunning()) {
        m_playbackNote = m_playback->deviceName();
        return;
    }
    // A missing output is retried now and then, not on every block.
    if (m_playbackRetry.isValid() && m_playbackRetry.elapsed() < kPlaybackRetryMs) {
        return;
    }
    m_playbackRetry.start();
    m_playbackNote = m_playback->start() ? m_playback->deviceName() : m_playback->errorString();
}

void AudioSpectrumWidget::onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                                        double value) {
    Q_UNUSED(timestampUs);
    if (!matches(binding)) {
        return;
    }
    if (binding.fieldId == m_config.rateFieldId) {
        if (value > 0.0 && std::fabs(value - m_rate) > 0.5) {
            // A new rate changes every bin: start the picture over.
            if (m_rate > 0.0 && std::fabs(value - m_rate) > m_rate * 0.005) {
                m_historyFill = 0;
                m_spectrumDb.clear();
                m_peakDb.clear();
            }
            m_rate = value;
        }
    } else if (binding.fieldId == m_config.seqFieldId) {
        const quint32 seq = quint32(value);
        if (m_haveSeq && seq != m_lastSeq + 1) {
            const quint32 gap = seq - m_lastSeq - 1;
            if (gap < kMaxGapBlocks) {
                m_lostBlocks += gap;
                m_pendingGap = qMin(gap, kMaxSilenceBlocks);
            } else if (m_playback != nullptr) {
                m_playback->buffer().clear();  // the device restarted
            }
        }
        m_lastSeq = seq;
        m_haveSeq = true;
    }
}

void AudioSpectrumWidget::onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                                        const QVector<float>& values) {
    Q_UNUSED(timestampUs);
    if (!matches(binding) || binding.fieldId != m_config.samplesFieldId || values.isEmpty() ||
        m_rate <= 0.0) {
        return;
    }
    ++m_receivedBlocks;
    const int count = values.size();
    const float gain = float(1.0 / m_config.fullScale);
    QVector<float> normalized(count);
    double sumSq = 0.0;
    for (int i = 0; i < count; ++i) {
        normalized[i] = values[i] * gain;
        sumSq += double(normalized[i]) * normalized[i];
    }
    const double rms = std::sqrt(sumSq / count);
    // dBFS of a sine: its RMS against a full-scale sine's (1 / sqrt(2)).
    m_blockDb =
        rms > 0.0 ? float(20.0 * std::log10(rms * std::sqrt(2.0))) : SpectrumAnalyzer::kFloorDb;

    applyPlayback();
    if (m_playback != nullptr && m_playback->isRunning()) {
        if (m_pendingGap != 0) {
            m_playback->buffer().pushSilence(int(m_pendingGap) * count);
        }
        m_playback->buffer().push(normalized.constData(), count, m_rate);
    }
    m_pendingGap = 0;

    if (m_paused) {
        return;
    }
    const int capacity = m_history.size();
    for (int i = 0; i < count; ++i) {
        m_history[m_historyPos] = normalized[i];
        m_historyPos = (m_historyPos + 1) % capacity;
    }
    m_historyFill = qMin(capacity, m_historyFill + count);
    m_dirty = true;
}

void AudioSpectrumWidget::refreshSpectrum() {
    m_dirty = false;
    const int n = m_analyzer.size();
    if (m_historyFill < n) {
        return;  // not enough samples for this FFT size yet
    }
    m_window.resize(n);
    const int capacity = m_history.size();
    const int start = (m_historyPos - n + capacity) % capacity;
    for (int i = 0; i < n; ++i) {
        m_window[i] = m_history[(start + i) % capacity];
    }
    m_spectrumDb.resize(m_analyzer.binCount());
    m_analyzer.compute(m_window.constData(), m_spectrumDb.data());
    if (m_peakDb.size() != m_spectrumDb.size()) {
        m_peakDb = m_spectrumDb;
    } else {
        for (int i = 0; i < m_peakDb.size(); ++i) {
            m_peakDb[i] = std::max(m_spectrumDb[i], m_peakDb[i] - kPeakFallDbPerTick);
        }
    }
}

float AudioSpectrumWidget::xForFrequency(double hz, const QRectF& plot) const {
    const double top = m_rate > 0.0 ? m_rate / 2.0 : 24000.0;
    if (m_view.logFrequency) {
        const double clamped = std::clamp(hz, kMinFrequency, top);
        return float(plot.left() + plot.width() * std::log(clamped / kMinFrequency) /
                                       std::log(top / kMinFrequency));
    }
    return float(plot.left() + plot.width() * std::clamp(hz, 0.0, top) / top);
}

void AudioSpectrumWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    paintChartBackground(painter, *this, palette);

    if (m_config.topicId == 0 || m_receivedBlocks == 0) {
        painter.setPen(palette.textSecondary);
        painter.drawText(rect(), Qt::AlignCenter,
                         m_config.topicId == 0 ? tr("Pick a stream topic in the properties")
                                               : tr("Waiting for audio..."));
        return;
    }

    // Same layout as the line chart (layoutCartesianChart): the info strip
    // takes the top legend row, the frequency labels the X axis band.
    const ChartStyle& style = chartStyle(chartStyleFromId(AppSettings::instance().chartStyleId()));
    const ChartColors colors = chartColors(style, palette);
    CartesianChrome chrome;
    chrome.xTickLabels = true;
    chrome.xTitle = style.defaultXAxisTitle;
    chrome.yTickLabels = true;
    chrome.yTitle = style.defaultYAxisTitle;
    chrome.gridDivisions = kDbGridDivisions;
    chrome.topLegendRow = true;
    chrome.bottomLegendRow = false;
    ChartAxisRequest dbAxis;
    dbAxis.range = {double(kMinDb), double(kMaxDb), AxisRange::Source::Fixed};
    dbAxis.title = tr("dBFS");
    // The frequency ticks are placed here, not by a ValueScale (whose
    // mapping is linear): the factory only makes the layout reserve the band.
    const ChartCartesianLayout layout = layoutCartesianChart(
        painter, rect(), {dbAxis}, chrome, style, [](int, int) { return ValueScale(); });
    const QRect& plotRect = layout.plotRect;
    const QRectF plot(plotRect);
    if (plot.width() < 20 || plot.height() < 20) {
        return;
    }
    const QFontMetricsF metrics(painter.font());

    // Info strip: rate, loudest frequency, block level | output, losses.
    const QRectF strip(chartOuterPadding(), chartOuterPadding(), width() - 2 * chartOuterPadding(),
                       layout.legendRowHeight);
    const double binHz = m_rate / m_analyzer.size();
    QString left = tr("%1 kHz").arg(m_rate / 1000.0, 0, 'f', 1);
    if (!m_spectrumDb.isEmpty()) {
        const auto loudest = std::max_element(m_spectrumDb.cbegin() + 1, m_spectrumDb.cend());
        const double hz = double(loudest - m_spectrumDb.cbegin()) * binHz;
        left += separator() + tr("peak %1 Hz").arg(frequencyLabel(hz));
    }
    left += separator() + tr("%1 dBFS").arg(double(m_blockDb), 0, 'f', 1);
    QString right = m_playbackNote;
    if (m_lostBlocks != 0) {
        right =
            tr("lost %1").arg(m_lostBlocks) + (right.isEmpty() ? QString() : separator() + right);
    }
    if (m_paused) {
        right = tr("paused") + (right.isEmpty() ? QString() : separator() + right);
    }
    painter.setPen(colors.legendText);
    painter.drawText(strip, Qt::AlignVCenter | Qt::AlignLeft, left);
    const QString elided = metrics.elidedText(
        right, Qt::ElideRight,
        qMax(0.0, strip.width() - metrics.horizontalAdvance(left) - kChartLegendItemGap));
    painter.drawText(strip, Qt::AlignVCenter | Qt::AlignRight, elided);

    const ValueScale& dbScale = layout.yAxes.first().scale;
    auto yFor = [&plotRect, &dbScale](float db) {
        return float(chartValueToY(plotRect, dbScale, std::clamp(db, kMinDb, kMaxDb)));
    };

    // Frequency grid: the decades (log) or even steps (linear). Drawn before
    // paintCartesianAxes() so it sits under the frame like the dB rows.
    const double top = m_rate / 2.0;
    QVector<double> ticks;
    if (m_view.logFrequency) {
        for (double decade = 10.0; decade <= top; decade *= 10.0) {
            for (double m : {1.0, 2.0, 5.0}) {
                if (decade * m >= kMinFrequency && decade * m <= top) {
                    ticks.append(decade * m);
                }
            }
        }
    } else {
        const double step = top > 20000.0 ? 10000.0 : (top > 5000.0 ? 2000.0 : 500.0);
        for (double f = step; f < top; f += step) {
            ticks.append(f);
        }
    }
    QVector<qreal> tickXs;
    for (double f : ticks) {
        const qreal x = xForFrequency(f, plot);
        if (x > plot.left() + 0.5 && x < plot.right() - 0.5) {
            tickXs.append(x);
        }
    }
    painter.setPen(QPen(colors.grid, 1, style.gridPenStyle));
    for (qreal x : tickXs) {
        painter.drawLine(QPointF(crispCoord(x), crispCoord(plot.top())),
                         QPointF(crispCoord(x), crispCoord(plot.bottom())));
    }

    // Grid rows, frame, the dB axis and the X axis title.
    paintCartesianAxes(painter, layout, chrome, style, colors, tr("Frequency (Hz)"));

    // X tick marks and labels, as paintCartesianAxes() draws a line chart's.
    painter.setPen(QPen(colors.frame, style.frameWidth));
    if (style.frame != ChartFrame::Ruler) {
        const bool inward = style.tickDirection == ChartTickDirection::Inward;
        const qreal bottom = crispCoord(plot.bottom());
        for (qreal x : tickXs) {
            painter.drawLine(QPointF(crispCoord(x), bottom),
                             QPointF(crispCoord(x), inward ? bottom - style.tickLength
                                                           : bottom + style.tickLength));
        }
    }
    painter.setFont(layout.tickFont);
    painter.setPen(colors.tickLabel);
    const QFontMetrics tickFm(layout.tickFont);
    const int labelTop = plotRect.bottom() + style.tickLabelOffset;
    int previousRight = plotRect.left();  // keeps clear of the bottom dB label
    for (int i = 0; i < ticks.size(); ++i) {
        const qreal x = xForFrequency(ticks[i], plot);
        const QString label = frequencyLabel(ticks[i]);
        const int w = tickFm.horizontalAdvance(label);
        QRect labelRect(qRound(x) - w / 2, labelTop, w, tickFm.height());
        if (labelRect.right() > rect().right() - 2) {
            labelRect.moveRight(rect().right() - 2);
        }
        if (labelRect.left() < previousRight + kChartAxisLabelGap) {
            continue;
        }
        painter.drawText(labelRect, Qt::AlignCenter, label);
        previousRight = labelRect.right();
    }
    painter.setFont(font());

    if (m_spectrumDb.isEmpty()) {
        return;
    }
    // One point per pixel column: the loudest bin under it, or the
    // interpolated value where a bin spans several pixels (low frequencies
    // on the log axis).
    auto curve = [&](const QVector<float>& db) {
        QPainterPath path;
        const int columns = int(plot.width());
        const int lastBin = db.size() - 1;
        for (int px = 0; px <= columns; ++px) {
            double f0;
            double f1;
            if (m_view.logFrequency) {
                const double ratio = std::log(top / kMinFrequency);
                f0 = kMinFrequency * std::exp(ratio * px / columns);
                f1 = kMinFrequency * std::exp(ratio * (px + 1) / columns);
            } else {
                f0 = top * px / columns;
                f1 = top * (px + 1) / columns;
            }
            const double b0 = f0 / binHz;
            const double b1 = f1 / binHz;
            float value;
            if (int(b1) > int(b0) + 1) {
                const int first = qBound(0, int(std::ceil(b0)), lastBin);
                const int last = qBound(0, int(b1), lastBin);
                value = *std::max_element(db.cbegin() + first, db.cbegin() + last + 1);
            } else {
                const int i = qBound(0, int(b0), lastBin);
                const int j = qMin(i + 1, lastBin);
                const float t = float(b0 - int(b0));
                value = db[i] + (db[j] - db[i]) * t;
            }
            const QPointF point(plot.left() + px, yFor(value));
            if (px == 0) {
                path.moveTo(point);
            } else {
                path.lineTo(point);
            }
        }
        return path;
    };

    painter.save();
    painter.setClipRect(plot);
    const QPainterPath line = curve(m_spectrumDb);
    QPainterPath fill = line;
    fill.lineTo(plot.right(), plot.bottom());
    fill.lineTo(plot.left(), plot.bottom());
    fill.closeSubpath();
    QColor area = palette.accent;
    area.setAlphaF(0.25);
    painter.fillPath(fill, area);
    painter.setPen(QPen(palette.accent, style.lineWidth));
    painter.drawPath(line);
    if (m_view.peakHold && m_peakDb.size() == m_spectrumDb.size()) {
        QColor peak = palette.textSecondary;
        peak.setAlphaF(0.7);
        painter.setPen(QPen(peak, 1));
        painter.drawPath(curve(m_peakDb));
    }
    painter.restore();
}

}  // namespace traceview

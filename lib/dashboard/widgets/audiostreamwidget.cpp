#include "audiostreamwidget.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "audioplayback.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kAnalysisMs = 33;  // ~30 pictures per second
constexpr double kMinFrequency = 20.0;
// A block counter jump larger than this is a restarted device, not loss.
constexpr quint32 kMaxGapBlocks = 1000;
// At most this many lost blocks are replayed as silence (the rest would
// only delay the sound).
constexpr quint32 kMaxSilenceBlocks = 8;
constexpr qint64 kPlaybackRetryMs = 5000;

const int kFftSizes[] = {1024, 2048, 4096, 8192, 16384};

namespace AudioViewKey {
constexpr char Play[] = "play";
constexpr char Fft[] = "fft";
constexpr char Axis[] = "axis";
}  // namespace AudioViewKey

quint32 parseHexId(const QJsonObject& json, const char* key) {
    return quint32(
        json.value(QLatin1String(key)).toString(QStringLiteral("0")).toULongLong(nullptr, 0));
}

AudioStreamView parseAudioView(const QJsonObject& json) {
    AudioStreamView view;
    view.play = json.value(QLatin1String(AudioViewKey::Play)).toBool(true);
    const int fft =
        json.value(QLatin1String(AudioViewKey::Fft)).toString(QStringLiteral("4096")).toInt();
    view.fftSize = 4096;
    for (int size : kFftSizes) {
        if (size == fft) {
            view.fftSize = size;
        }
    }
    view.logFrequency = json.value(QLatin1String(AudioViewKey::Axis))
                            .toString(QStringLiteral("log")) != QLatin1String("linear");
    return view;
}

void writeAudioView(const AudioStreamView& view, QJsonObject& json) {
    json[QLatin1String(AudioViewKey::Play)] = view.play;
    json[QLatin1String(AudioViewKey::Fft)] = QString::number(view.fftSize);
    json[QLatin1String(AudioViewKey::Axis)] =
        view.logFrequency ? QStringLiteral("log") : QStringLiteral("linear");
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

AudioStreamWidget::AudioStreamWidget(QWidget* parent)
    : StyledChartWidget(parent), m_history(SpectrumAnalyzer::kMaxSize, 0.0f) {
    setMinimumSize(160, 90);
    setMouseTracking(true);
    m_analyzer.setSize(m_audio.fftSize);
    m_analysisTimer = new QTimer(this);
    m_analysisTimer->setInterval(kAnalysisMs);
    connect(m_analysisTimer, &QTimer::timeout, this, [this] {
        if (m_dirty && !m_paused) {
            m_dirty = false;
            analysisTick();
            update();
        }
    });
    m_analysisTimer->start();
    // A frequency axis wants its values, and these widgets their readouts
    // and a filled curve: shown unless the saved view says otherwise (the
    // line chart starts with all three off).
    m_view.showXTickLabels = true;
    m_view.showInfoRow = true;
    m_view.fillArea = true;
}

AudioStreamWidget::~AudioStreamWidget() = default;

QString AudioStreamWidget::frequencyLabel(double hz) {
    if (hz >= 1000.0) {
        const double k = hz / 1000.0;
        return QStringLiteral("%1k").arg(k, 0, 'g', k >= 10.0 ? 3 : 2);
    }
    return QString::number(int(std::lround(hz)));
}

void AudioStreamWidget::setConfig(const QJsonObject& config) {
    const AudioSpectrumConfig next = parseAudioSpectrumConfig(config);
    const bool rebound = next.sourceId != m_config.sourceId || next.topicId != m_config.topicId;
    m_config = next;
    if (rebound) {
        resetStream();
    }
    setViewConfig(config.value(QLatin1String("view")).toObject());
}

void AudioStreamWidget::setViewConfig(const QJsonObject& view) {
    m_view = parseChartViewOptions(view);
    if (!view.contains(QLatin1String(ChartViewKey::XTickLabels))) {
        m_view.showXTickLabels = true;
    }
    if (!view.contains(QLatin1String(ChartViewKey::InfoRow))) {
        m_view.showInfoRow = true;
    }
    if (!view.contains(QLatin1String(ChartViewKey::FillArea))) {
        m_view.fillArea = true;
    }
    const AudioStreamView previous = m_audio;
    m_audio = parseAudioView(view);
    readExtraView(view);
    if (m_analyzer.size() != m_audio.fftSize) {
        m_analyzer.setSize(m_audio.fftSize);
    }
    if (previous.fftSize != m_audio.fftSize || previous.logFrequency != m_audio.logFrequency) {
        // The samples still hold: only what was derived from them goes.
        analysisReset();
        m_dirty = m_historyFill > 0;
    }
    applyPlayback();
    update();
}

QJsonObject AudioStreamWidget::currentViewJson() const {
    QJsonObject json = chartViewOptionsToJson(m_view);
    writeAudioView(m_audio, json);
    writeExtraView(json);
    return json;
}

bool AudioStreamWidget::isChartViewKey(const QString& id) const {
    for (const WidgetViewOption& option : chartViewOptionList(m_view, viewFeatures())) {
        if (option.id == id) {
            return true;
        }
    }
    return false;
}

QVector<WidgetViewOption> AudioStreamWidget::viewOptions() const {
    QVector<WidgetViewOption> options =
        leadingViewOptions() + chartViewOptionList(m_view, viewFeatures());

    WidgetViewOption play;
    play.id = QLatin1String(AudioViewKey::Play);
    play.label = tr("Play audio");
    play.value = m_audio.play;
    play.startsSection = true;
    options.append(play);

    WidgetViewOption fft;
    fft.id = QLatin1String(AudioViewKey::Fft);
    fft.label = tr("FFT size:");
    fft.kind = WidgetViewOption::Kind::Choice;
    fft.value = QString::number(m_audio.fftSize);
    for (int size : kFftSizes) {
        fft.choices.append({QString::number(size), tr("%1 points").arg(size)});
    }
    fft.startsSection = true;
    options.append(fft);

    WidgetViewOption axis;
    axis.id = QLatin1String(AudioViewKey::Axis);
    axis.label = tr("Frequency axis:");
    axis.kind = WidgetViewOption::Kind::Choice;
    axis.value = m_audio.logFrequency ? QStringLiteral("log") : QStringLiteral("linear");
    axis.choices = {{QStringLiteral("log"), tr("Logarithmic")},
                    {QStringLiteral("linear"), tr("Linear")}};
    options.append(axis);

    options += extraViewOptions();
    return options;
}

QJsonObject AudioStreamWidget::viewConfigWith(const QString& id, const QVariant& value) const {
    if (isChartViewKey(id)) {
        QJsonObject json = chartViewOptionsToJson(withChartViewOption(m_view, id, value));
        writeAudioView(m_audio, json);
        writeExtraView(json);
        return json;
    }
    // Audio options store what the menu hands back: a bool for a toggle,
    // the choice id for a select.
    QJsonObject json = currentViewJson();
    for (const WidgetViewOption& option : viewOptions()) {
        if (option.id == id) {
            json[id] = option.kind == WidgetViewOption::Kind::Toggle
                           ? QJsonValue(value.toBool())
                           : QJsonValue(value.toString());
        }
    }
    return json;
}

void AudioStreamWidget::setPaused(bool paused) {
    m_paused = paused;
    update();
}

void AudioStreamWidget::clearChartData() {
    m_lostBlocks = 0;
    m_receivedBlocks = 0;
    m_blockDb = SpectrumAnalyzer::kFloorDb;
    resetAnalysis();
    update();
}

void AudioStreamWidget::resetAnalysis() {
    m_historyFill = 0;
    m_dirty = false;
    analysisReset();
}

void AudioStreamWidget::resetStream() {
    m_rate = 0.0;
    m_haveSeq = false;
    m_pendingGap = 0;
    clearChartData();
    if (m_playback != nullptr) {
        m_playback->buffer().clear();
    }
}

void AudioStreamWidget::applyPlayback() {
    const bool wanted = m_audio.play && m_config.topicId != 0;
    if (!wanted) {
        if (m_playback != nullptr && m_playback->isRunning()) {
            m_playback->stop();
        }
        m_playbackNote = m_audio.play ? QString() : tr("muted");
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

void AudioStreamWidget::onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                                      double value) {
    Q_UNUSED(timestampUs);
    if (!matches(binding)) {
        return;
    }
    if (binding.fieldId == m_config.rateFieldId) {
        if (value > 0.0 && std::fabs(value - m_rate) > 0.5) {
            // A new rate changes every bin: start the picture over.
            const bool restart = m_rate > 0.0 && std::fabs(value - m_rate) > m_rate * 0.005;
            m_rate = value;
            if (restart) {
                resetAnalysis();
            }
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

void AudioStreamWidget::onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
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
    samplesAppended(count);
}

float AudioStreamWidget::historySample(int back) const {
    const int capacity = m_history.size();
    return m_history[((m_historyPos - 1 - back) % capacity + capacity) % capacity];
}

bool AudioStreamWidget::fillWindow(int endOffset) {
    const int n = m_analyzer.size();
    if (endOffset < 0 || m_historyFill < n + endOffset) {
        return false;
    }
    m_window.resize(n);
    const int capacity = m_history.size();
    const int start = ((m_historyPos - endOffset - n) % capacity + capacity) % capacity;
    for (int i = 0; i < n; ++i) {
        m_window[i] = m_history[(start + i) % capacity];
    }
    return true;
}

void AudioStreamWidget::computeSpectrum(QVector<float>& outDb) {
    outDb.resize(m_analyzer.binCount());
    m_analyzer.compute(m_window.constData(), outDb.data());
}

double AudioStreamWidget::topFrequency() const {
    return m_rate > 0.0 ? m_rate / 2.0 : 24000.0;
}

double AudioStreamWidget::frequencyAt(double t) const {
    const double top = topFrequency();
    if (m_audio.logFrequency) {
        return kMinFrequency * std::exp(std::log(top / kMinFrequency) * t);
    }
    return top * t;
}

double AudioStreamWidget::frequencyFraction(double hz) const {
    const double top = topFrequency();
    if (m_audio.logFrequency) {
        const double clamped = std::clamp(hz, kMinFrequency, top);
        return std::log(clamped / kMinFrequency) / std::log(top / kMinFrequency);
    }
    return std::clamp(hz, 0.0, top) / top;
}

float AudioStreamWidget::levelBetween(const QVector<float>& db, double f0, double f1) const {
    const double binHz = (m_rate > 0.0 ? m_rate : 48000.0) / m_analyzer.size();
    const int lastBin = db.size() - 1;
    const double b0 = f0 / binHz;
    const double b1 = f1 / binHz;
    if (int(b1) > int(b0) + 1) {
        const int first = qBound(0, int(std::ceil(b0)), lastBin);
        const int last = qBound(0, int(b1), lastBin);
        return *std::max_element(db.cbegin() + first, db.cbegin() + last + 1);
    }
    const int i = qBound(0, int(b0), lastBin);
    const int j = qMin(i + 1, lastBin);
    const float t = float(b0 - int(b0));
    return db[i] + (db[j] - db[i]) * t;
}

QVector<double> AudioStreamWidget::frequencyTicks() const {
    const double top = topFrequency();
    QVector<double> ticks;
    if (m_audio.logFrequency) {
        for (double decade = 10.0; decade <= top; decade *= 10.0) {
            for (double m : {1.0, 2.0, 5.0}) {
                if (decade * m >= kMinFrequency && decade * m <= top) {
                    ticks.append(decade * m);
                }
            }
        }
    } else {
        const double step = top > 20000.0 ? 5000.0 : (top > 5000.0 ? 2000.0 : 500.0);
        for (double f = 0.0; f < top; f += step) {
            ticks.append(f);
        }
    }
    return ticks;
}

bool AudioStreamWidget::paintPlaceholder(QPainter& painter) {
    if (m_config.topicId != 0 && m_receivedBlocks != 0) {
        return false;
    }
    painter.setPen(ThemeManager::instance().currentTheme().textSecondary);
    painter.drawText(rect(), Qt::AlignCenter,
                     m_config.topicId == 0 ? tr("Pick a stream topic in the properties")
                                           : tr("Waiting for audio..."));
    return true;
}

CartesianChrome AudioStreamWidget::audioChrome(int gridDivisions) const {
    CartesianChrome chrome;
    chrome.xTickLabels = m_view.showXTickLabels;
    chrome.xTitle = m_view.showXAxisTitle;
    chrome.yTickLabels = m_view.showYTickLabels;
    chrome.yTitle = m_view.showYAxisTitle;
    chrome.gridDivisions = gridDivisions;
    chrome.yTickCount = m_view.yTickCount;
    chrome.topLegendRow = false;
    chrome.bottomLegendRow = false;
    chrome.infoRows = m_view.showInfoRow ? 1 + extraInfoRows() : 0;
    return chrome;
}

void AudioStreamWidget::paintInfoRow(QPainter& painter, const ChartCartesianLayout& layout,
                                     const ChartColors& colors, double peakHz,
                                     const QVector<ChartInfoField>& extra) {
    if (!m_view.showInfoRow) {
        return;
    }
    // Fixed formats and slots as wide as the widest value, so the row
    // doesn't shift as the numbers change.
    QVector<ChartInfoField> fields;
    if (m_view.infoItems.contains(QLatin1String("rate"))) {
        fields.append({tr("%1 kHz"), QString::number(m_rate / 1000.0, 'f', 1),
                       QStringLiteral("00.0")});
    }
    if (peakHz >= 0.0) {
        fields.append({tr("peak %1 Hz"), QString::number(qRound(peakHz)),
                       QStringLiteral("00000")});
    }
    fields.append({tr("%1 dBFS"), QString::number(double(m_blockDb), 'f', 1),
                   QStringLiteral("-000.0")});
    fields += extra;
    QStringList right;
    if (m_paused) {
        right << tr("paused");
    }
    if (m_lostBlocks != 0) {
        right << tr("lost %1").arg(m_lostBlocks);
    }
    if (!m_playbackNote.isEmpty()) {
        right << m_playbackNote;
    }
    paintChartInfoRow(painter, layout, 0, fields, right.join(QStringLiteral("  ·  ")), colors);
}

void AudioStreamWidget::paintHoverBalloon(QPainter& painter, const QRect& plotRect,
                                          const QStringList& lines, bool verticalLine) {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    if (verticalLine) {
        painter.setPen(QPen(palette.textSecondary, 1, Qt::DashLine));
        painter.drawLine(QPointF(crispCoord(m_hoverPos.x()), plotRect.top()),
                         QPointF(crispCoord(m_hoverPos.x()), plotRect.bottom()));
    }
    if (lines.isEmpty()) {
        return;
    }
    // Same balloon as the line chart's hover crosshair.
    constexpr int kBalloonPadding = 6;
    constexpr int kBalloonGap = 14;  // clear of the cursor hotspot
    const QFontMetrics fm(painter.font());
    int textWidth = 0;
    for (const QString& line : lines) {
        textWidth = qMax(textWidth, fm.horizontalAdvance(line));
    }
    const int balloonWidth = kBalloonPadding * 2 + textWidth;
    const int balloonHeight = kBalloonPadding * 2 + fm.height() * lines.size();
    QRect balloon(m_hoverPos.x() + kBalloonGap, m_hoverPos.y() - balloonHeight / 2, balloonWidth,
                  balloonHeight);
    if (balloon.right() > plotRect.right()) {
        balloon.moveLeft(m_hoverPos.x() - kBalloonGap - balloonWidth);
    }
    balloon.moveLeft(qMax(balloon.left(), plotRect.left()));
    balloon.moveRight(qMin(balloon.right(), plotRect.right()));
    balloon.moveTop(qMax(balloon.top(), plotRect.top()));
    balloon.moveBottom(qMin(balloon.bottom(), plotRect.bottom()));

    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(palette.surface);
    painter.setOpacity(0.95);
    painter.drawRoundedRect(balloon, 4, 4);
    painter.setOpacity(1.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(palette.textPrimary);
    for (int i = 0; i < lines.size(); ++i) {
        painter.drawText(QRect(balloon.left() + kBalloonPadding,
                               balloon.top() + kBalloonPadding + i * fm.height(), textWidth,
                               fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }
}

void AudioStreamWidget::mouseMoveEvent(QMouseEvent* event) {
    m_hoverPos = event->pos();
    m_hasHoverPos = true;
    if (m_view.showHoverCrosshair) {
        update();
    }
    StyledChartWidget::mouseMoveEvent(event);
}

void AudioStreamWidget::leaveEvent(QEvent* event) {
    m_hasHoverPos = false;
    if (m_view.showHoverCrosshair) {
        update();
    }
    StyledChartWidget::leaveEvent(event);
}

}  // namespace traceview

#include "chartwidgets.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QTimer>
#include <QtMath>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kLabelMargin = 8;
// Fixed left gutter for Y-axis value labels, to the left of plotRect. Not
// measured against the actual label text (font metrics vary per value) --
// a fixed width matches how kLabelMargin/kHeaderHeight etc. are already
// plain constants elsewhere in this codebase rather than dynamically sized.
constexpr int kAxisGutter = 40;
// Caps how often onSerialPayload() triggers an actual repaint, independent
// of how fast frames arrive (see BACKEND_TODO.txt "taxas diferentes por
// widget") -- data still gets appended to the buffers on every frame.
constexpr int kRepaintIntervalMs = 33; // ~30 Hz

// Rounded to the same curve as the DashboardCell wrapped around `widget` --
// via contentFillPath(), spanning `widget`'s true bounds, not an inset
// rect -- so straight edges stay flush with the cell's true edge and the
// DashboardCell overlay can draw the outer outline above it. A plain
// drawRect() here would run straight into the corner
// well past that curve, leaving a few pixels of straight edge poking out
// past the rounded outline. See "Corner radius" in docs/VISUAL_IDENTITY.md.
void paintBackground(QPainter& painter, const DashboardWidget& widget, const ThemePalette& palette) {
    painter.fillPath(widget.contentFillPath(), palette.surface);

    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    // Inset by 0.5 so the 1px pen renders at full strength instead of half
    // of it landing outside this widget's own paint device and getting
    // clipped -- same idea as DashboardCell's own outline stroke inset,
    // just for this widget's separate, decorative inner border.
    const QRectF strokeRect = QRectF(widget.rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.drawPath(widget.roundedPath(strokeRect));
}

// Three horizontal gridlines (min/mid/max) across plotRect's width, each
// with its value labeled in the kAxisGutter-wide strip to plotRect's left.
// Deliberately just 3 -- a busier grid would fight the throttled, frequently
// -redrawn plot lines/bars for attention on a widget this small.
void paintYAxis(QPainter& painter, const QRect& plotRect, double yMin, double yMax, const QString& unit,
                 const ThemePalette& palette) {
    if (plotRect.height() <= 0 || plotRect.width() <= 0) {
        return;
    }
    const int midY = plotRect.center().y();

    painter.setPen(QPen(palette.border, 1));
    painter.drawLine(plotRect.left(), plotRect.top(), plotRect.right(), plotRect.top());
    painter.drawLine(plotRect.left(), midY, plotRect.right(), midY);
    painter.drawLine(plotRect.left(), plotRect.bottom(), plotRect.right(), plotRect.bottom());

    painter.setPen(palette.textSecondary);
    const QFontMetrics fm(painter.font());
    const QRect gutter(0, 0, plotRect.left() - kLabelMargin, fm.height());
    auto drawValue = [&](double value, int centerY) {
        painter.drawText(QRect(gutter.x(), centerY - gutter.height() / 2, gutter.width(), gutter.height()),
                          Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'g', 4) + unit);
    };
    drawValue(yMax, plotRect.top());
    drawValue((yMin + yMax) / 2.0, midY);
    drawValue(yMin, plotRect.bottom());
}

// Series name + color swatch, one per configured series, in a single row
// clipped to `area`'s width -- overflow just stops adding entries rather
// than wrapping or eliding, which is enough for the handful of series a
// chart this size realistically holds.
void paintLegend(QPainter& painter, const QRect& area, const QVector<ChartSeriesConfig>& seriesConfigs,
                  const ThemePalette& palette) {
    if (seriesConfigs.isEmpty()) {
        return;
    }
    constexpr int kSwatchSize = 8;
    constexpr int kItemGap = 14;
    const QFontMetrics fm(painter.font());
    const int rowHeight = qMax(kSwatchSize, fm.height());
    const int y = area.top() + kLabelMargin;
    const int rightBound = area.right() - kLabelMargin;
    int x = area.left() + kLabelMargin;

    for (const ChartSeriesConfig& series : seriesConfigs) {
        const QString name = series.name.isEmpty() ? QString("Series %1").arg(series.index + 1) : series.name;
        const int textWidth = fm.horizontalAdvance(name);
        const int itemWidth = kSwatchSize + 4 + textWidth;
        if (x + itemWidth > rightBound) {
            break;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(series.color);
        painter.drawEllipse(QRect(x, y + (rowHeight - kSwatchSize) / 2, kSwatchSize, kSwatchSize));

        painter.setPen(palette.textSecondary);
        painter.drawText(QRect(x + kSwatchSize + 4, y, textWidth, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, name);

        x += itemWidth + kItemGap;
    }
}

Qt::PenStyle qtPenStyleFor(ChartSeriesStyle style) {
    switch (style) {
        case ChartSeriesStyle::Dashed:
            return Qt::DashLine;
        case ChartSeriesStyle::Dotted:
            return Qt::DotLine;
        case ChartSeriesStyle::DashDot:
            return Qt::DashDotLine;
        default:
            return Qt::SolidLine;
    }
}

bool isMarkerStyle(ChartSeriesStyle style) {
    return style == ChartSeriesStyle::Cross || style == ChartSeriesStyle::Asterisk;
}

// Y range to map buffered values into plotRect's height: the configured
// fixed range, or an auto range spanning whatever's currently buffered
// (with a little headroom so extreme points don't touch the plot edges).
QPair<double, double> computeYRange(const ChartConfig& config, const QVector<QVector<double>>& buffers) {
    if (config.yAxisMode == ChartYAxisMode::Fixed) {
        return {config.yMin, qMax(config.yMax, config.yMin + 1e-6)};
    }

    bool any = false;
    double lo = 0.0;
    double hi = 0.0;
    for (const QVector<double>& buffer : buffers) {
        for (double value : buffer) {
            if (!any) {
                lo = hi = value;
                any = true;
            } else {
                lo = qMin(lo, value);
                hi = qMax(hi, value);
            }
        }
    }
    if (!any) {
        return {0.0, 1.0};
    }
    if (qFuzzyCompare(lo, hi)) {
        return {lo - 1.0, hi + 1.0};
    }
    const double pad = (hi - lo) * 0.05;
    return {lo - pad, hi + pad};
}

// Pixel step between adjacent samples, sized against the series' full
// capacity (not however many samples are buffered yet) so points always
// anchor to the right edge and scroll left as new data arrives, rather than
// rescaling every time a not-yet-full buffer grows.
qreal xStepFor(const QRect& plotRect, int capacity) {
    return capacity > 1 ? qreal(plotRect.width()) / qreal(capacity - 1) : 0.0;
}

void paintLineSeries(QPainter& painter, const QRect& plotRect, int capacity, const ChartSeriesConfig& seriesConfig,
                      const QVector<double>& values, double yMin, double yMax) {
    if (values.isEmpty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal step = xStepFor(plotRect, capacity);

    auto pointAt = [&](int i) -> QPointF {
        const qreal x = plotRect.right() - step * (values.size() - 1 - i);
        const qreal t = (values[i] - yMin) / yRange;
        const qreal y = plotRect.bottom() - plotRect.height() * t;
        return QPointF(x, y);
    };

    if (isMarkerStyle(seriesConfig.style)) {
        painter.setPen(QPen(seriesConfig.color, 2));
        constexpr qreal kMarkerSize = 4.0;
        for (int i = 0; i < values.size(); ++i) {
            const QPointF p = pointAt(i);
            painter.drawLine(QPointF(p.x() - kMarkerSize, p.y()), QPointF(p.x() + kMarkerSize, p.y()));
            painter.drawLine(QPointF(p.x(), p.y() - kMarkerSize), QPointF(p.x(), p.y() + kMarkerSize));
            if (seriesConfig.style == ChartSeriesStyle::Asterisk) {
                const qreal d = kMarkerSize * 0.7;
                painter.drawLine(QPointF(p.x() - d, p.y() - d), QPointF(p.x() + d, p.y() + d));
                painter.drawLine(QPointF(p.x() - d, p.y() + d), QPointF(p.x() + d, p.y() - d));
            }
        }
        return;
    }

    QPainterPath path;
    for (int i = 0; i < values.size(); ++i) {
        const QPointF p = pointAt(i);
        if (i == 0) {
            path.moveTo(p);
        } else {
            path.lineTo(p);
        }
    }
    painter.setPen(QPen(seriesConfig.color, 2, qtPenStyleFor(seriesConfig.style)));
    painter.drawPath(path);
}

void paintBarSeries(QPainter& painter, const QRect& plotRect, int capacity,
                     const QVector<ChartSeriesConfig>& seriesConfigs, const QVector<QVector<double>>& buffers,
                     double yMin, double yMax) {
    if (capacity <= 0 || seriesConfigs.isEmpty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal rawStep = xStepFor(plotRect, capacity);
    const qreal step = rawStep > 0 ? rawStep : qreal(plotRect.width()) / qreal(qMax(1, capacity));
    constexpr qreal kGroupGapFraction = 0.15;
    const qreal groupWidth = step * (1.0 - kGroupGapFraction);
    const qreal barWidth = qMax(1.0, groupWidth / seriesConfigs.size());
    const qreal zeroT = qBound(0.0, (0.0 - yMin) / yRange, 1.0);
    const qreal zeroY = plotRect.bottom() - plotRect.height() * zeroT;

    painter.setPen(Qt::NoPen);
    for (int series = 0; series < seriesConfigs.size() && series < buffers.size(); ++series) {
        const QVector<double>& values = buffers[series];
        for (int i = 0; i < values.size(); ++i) {
            const int distanceFromNewest = values.size() - 1 - i;
            if (distanceFromNewest >= capacity) {
                continue;
            }
            const qreal groupRight = plotRect.right() - step * distanceFromNewest;
            const qreal x = groupRight - groupWidth + series * barWidth;
            const qreal t = qBound(0.0, (values[i] - yMin) / yRange, 1.0);
            const qreal barTop = plotRect.bottom() - plotRect.height() * t;

            painter.setBrush(seriesConfigs[series].color);
            painter.drawRect(QRectF(x, qMin(barTop, zeroY), barWidth, qAbs(barTop - zeroY)));
        }
    }
}

} // namespace

void ChartWidgetBase::setConfig(const QJsonObject& config) {
    const ChartConfig newConfig = parseChartConfig(config);
    m_seriesBuffers = resizeChartBuffers(m_seriesBuffers, newConfig);
    m_config = newConfig;
    update();
}

void ChartWidgetBase::onSerialPayload(const QByteArray& payload) {
    appendChartSample(m_seriesBuffers, m_config, payload);
    if (m_repaintPending) {
        return;
    }
    m_repaintPending = true;
    QTimer::singleShot(kRepaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

DummyLineChartWidget::DummyLineChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

void DummyLineChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    const QRect plotRect = area.adjusted(kLabelMargin + kAxisGutter, kLabelMargin * 3, -kLabelMargin, -kLabelMargin);
    const int capacity = chartBufferCapacity(m_config);
    const auto [yMin, yMax] = computeYRange(m_config, m_seriesBuffers);

    paintYAxis(painter, plotRect, yMin, yMax, m_config.yUnit, palette);
    for (int i = 0; i < m_config.series.size() && i < m_seriesBuffers.size(); ++i) {
        paintLineSeries(painter, plotRect, capacity, m_config.series[i], m_seriesBuffers[i], yMin, yMax);
    }

    // No more redundant "Line Chart" corner label -- DashboardCell's header
    // already shows the configured name (TAREFA 1); this corner now carries
    // the per-series legend instead, which the old literal text had no room
    // for anyway.
    paintLegend(painter, area, m_config.series, palette);
}

DummyBarChartWidget::DummyBarChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

void DummyBarChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    const QRect plotRect = area.adjusted(kLabelMargin + kAxisGutter, kLabelMargin * 3, -kLabelMargin, -kLabelMargin);
    const int capacity = chartBufferCapacity(m_config);
    const auto [yMin, yMax] = computeYRange(m_config, m_seriesBuffers);

    paintYAxis(painter, plotRect, yMin, yMax, m_config.yUnit, palette);
    paintBarSeries(painter, plotRect, capacity, m_config.series, m_seriesBuffers, yMin, yMax);

    // See DummyLineChartWidget::paintEvent for why this is a legend instead
    // of a "Bar Chart" corner label now.
    paintLegend(painter, area, m_config.series, palette);
}

DummyGaugeWidget::DummyGaugeWidget(QWidget* parent) : DashboardWidget(parent) {}

void DummyGaugeWidget::setConfig(const QJsonObject& config) {
    m_config = parseGaugeConfig(config);
    update();
}

void DummyGaugeWidget::onSerialPayload(const QByteArray& payload) {
    m_value = decodeGaugeValue(payload, m_config);
    if (m_repaintPending) {
        return;
    }
    m_repaintPending = true;
    QTimer::singleShot(kRepaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

void DummyGaugeWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    // No more redundant "Gauge" corner label -- DashboardCell's header
    // already shows the configured name (TAREFA 1), same reasoning as the
    // line/bar charts in TAREFA 3. Unlike those, a gauge has no per-series
    // legend to put in its place, so the reclaimed margin just goes back to
    // the arc instead of being pinned at kLabelMargin * 3.
    const QRect plotRect = area.adjusted(kLabelMargin, kLabelMargin, -kLabelMargin, -kLabelMargin);
    const int side = qMin(plotRect.width(), plotRect.height());
    if (side > 0) {
        const bool hasValue = !qIsNaN(m_value);
        const double range = m_config.max - m_config.min;
        const double fraction = hasValue && range != 0.0 ? qBound(0.0, (m_value - m_config.min) / range, 1.0) : 0.0;

        const QRect arcRect(plotRect.center().x() - side / 2, plotRect.top(), side, side);
        constexpr int kStartAngle = 90 * 16;   // 12 o'clock, Qt angles are in 1/16th degrees
        constexpr int kSpanAngle = -270 * 16;  // sweep 270 degrees clockwise

        QPen trackPen(palette.surfaceAlt, 8, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawArc(arcRect.adjusted(4, 4, -4, -4), kStartAngle, kSpanAngle);

        if (hasValue) {
            QPen valuePen(palette.accent, 8, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(valuePen);
            painter.drawArc(arcRect.adjusted(4, 4, -4, -4), kStartAngle, qRound(kSpanAngle * fraction));
        }

        // The single most important number on this widget -- larger, bold,
        // the one deliberate typography accent in the app (see "Typography"
        // in docs/VISUAL_IDENTITY.md); every other label stays system
        // default weight/size.
        QFont valueFont = painter.font();
        valueFont.setPointSize(valueFont.pointSize() + 6);
        valueFont.setBold(true);
        painter.setFont(valueFont);
        painter.setPen(palette.textPrimary);
        const QString text =
            hasValue ? QString("%1%2").arg(m_value, 0, 'f', m_config.decimals).arg(m_config.unit) : "--";
        painter.drawText(arcRect, Qt::AlignCenter, text);
    }
}

} // namespace traceview

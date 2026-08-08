#include "chartwidgets.h"

#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QtMath>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kLabelMargin = 8;

void paintBackground(QPainter& painter, const QRect& rect, const ThemePalette& palette) {
    painter.fillRect(rect, palette.surface);
    painter.setPen(QPen(palette.border, 1));
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
}

void paintLabel(QPainter& painter, const QRect& rect, const QString& text, const ThemePalette& palette) {
    painter.setPen(palette.textSecondary);
    painter.drawText(rect.adjusted(kLabelMargin, kLabelMargin, -kLabelMargin, -kLabelMargin),
                      Qt::AlignTop | Qt::AlignLeft, text);
}

QVector<double> randomSeries(int count, quint32 seed) {
    QRandomGenerator rng(seed);
    QVector<double> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i) {
        values.append(rng.bounded(100) / 100.0);
    }
    return values;
}

} // namespace

DummyLineChartWidget::DummyLineChartWidget(QWidget* parent) : DashboardWidget(parent) {}

void DummyLineChartWidget::paintEvent(QPaintEvent*) {
    static const QVector<double> kPoints = randomSeries(12, 42);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, area, palette);

    const QRect plotRect = area.adjusted(kLabelMargin, kLabelMargin * 3, -kLabelMargin, -kLabelMargin);
    if (plotRect.width() > 0 && plotRect.height() > 0 && kPoints.size() > 1) {
        QPainterPath path;
        for (int i = 0; i < kPoints.size(); ++i) {
            const qreal x = plotRect.left() + plotRect.width() * (qreal(i) / (kPoints.size() - 1));
            const qreal y = plotRect.bottom() - plotRect.height() * kPoints[i];
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(palette.accent, 2));
        painter.drawPath(path);
    }

    paintLabel(painter, area, "Line Chart", palette);
}

DummyBarChartWidget::DummyBarChartWidget(QWidget* parent) : DashboardWidget(parent) {}

void DummyBarChartWidget::paintEvent(QPaintEvent*) {
    static const QVector<double> kBars = randomSeries(6, 7);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, area, palette);

    const QRect plotRect = area.adjusted(kLabelMargin, kLabelMargin * 3, -kLabelMargin, -kLabelMargin);
    if (plotRect.width() > 0 && plotRect.height() > 0 && !kBars.isEmpty()) {
        const qreal gap = 4.0;
        const qreal barWidth = (plotRect.width() - gap * (kBars.size() - 1)) / kBars.size();
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.accent);
        for (int i = 0; i < kBars.size(); ++i) {
            const qreal x = plotRect.left() + i * (barWidth + gap);
            const qreal h = plotRect.height() * kBars[i];
            painter.drawRect(QRectF(x, plotRect.bottom() - h, barWidth, h));
        }
    }

    paintLabel(painter, area, "Bar Chart", palette);
}

DummyGaugeWidget::DummyGaugeWidget(QWidget* parent) : DashboardWidget(parent) {}

void DummyGaugeWidget::paintEvent(QPaintEvent*) {
    constexpr double kValue = 0.62;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, area, palette);

    const QRect plotRect = area.adjusted(kLabelMargin, kLabelMargin * 3, -kLabelMargin, -kLabelMargin);
    const int side = qMin(plotRect.width(), plotRect.height());
    if (side > 0) {
        const QRect arcRect(plotRect.center().x() - side / 2, plotRect.top(), side, side);
        constexpr int kStartAngle = 90 * 16;   // 12 o'clock, Qt angles are in 1/16th degrees
        constexpr int kSpanAngle = -270 * 16;  // sweep 270 degrees clockwise

        QPen trackPen(palette.surfaceAlt, 8, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawArc(arcRect.adjusted(4, 4, -4, -4), kStartAngle, kSpanAngle);

        QPen valuePen(palette.accent, 8, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(valuePen);
        painter.drawArc(arcRect.adjusted(4, 4, -4, -4), kStartAngle, qRound(kSpanAngle * kValue));

        painter.setPen(palette.textPrimary);
        painter.drawText(arcRect, Qt::AlignCenter, QString("%1%").arg(qRound(kValue * 100)));
    }

    paintLabel(painter, area, "Gauge", palette);
}

} // namespace traceview

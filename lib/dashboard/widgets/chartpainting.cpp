#include "chartpainting.h"

#include <QCoreApplication>
#include <QPainter>
#include <QtMath>
#include <limits>

#include "dashboard/dashboardwidget.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kLegendDotSize = 8;
constexpr int kLegendLineSampleWidth = 16;
// Ticks per Y label: one tick every ~3 text lines keeps the labels apart.
constexpr int kYTickSpacingLines = 3;
// Extra room between two stacked Y axes, so one axis' labels don't run
// into the next one's spine when the axis titles are hidden.
constexpr int kStackedAxisGap = 10;
// In-plot legend box: inset from the plot edges, inner padding, row gap.
constexpr int kInsetLegendMargin = 6;
constexpr int kInsetLegendPadding = 6;
constexpr int kInsetLegendRowGap = 2;
// Between two info rows.
constexpr int kInfoRowGap = 2;

QColor axisLabelColor(const ChartValueAxis& axis, const ChartColors& colors) {
    return axis.labelColor.isValid() ? axis.labelColor : colors.tickLabel;
}

// Value-axis ticks always point outward, toward their own numbers, on every
// axis -- a lone one or a stack -- so every Y axis looks the same. An inward
// style (Engineering) keeps its inward ticks on the X axis and on the
// mirrored right/top edges only, and pushes the Y labels past the tick.
int yLabelOffset(const ChartStyle& style) {
    if (style.tickDirection == ChartTickDirection::Inward) {
        return style.tickLength + kChartAxisLabelGap;
    }
    return style.tickLabelOffset;
}

// A tick or gridline position snapped onto the frame's own pixel grid.
// chartValueToY() puts the top of the range one pixel above plotRect.top()
// (bottom() - height()), so without the clamp the top tick and gridline
// poked out above the frame.
qreal plotCrispY(const QRect& plot, qreal y) {
    return crispCoord(qBound(qreal(plot.top()), y, qreal(plot.bottom())));
}

qreal plotCrispX(const QRect& plot, qreal x) {
    return crispCoord(qBound(qreal(plot.left()), x, qreal(plot.right())));
}

// Framed styles mark where the two axes meet with one tick per axis at the
// bottom-left corner, always pointing outward (Y to the left, X down): an
// inward tick there would run along the frame itself and vanish. Kept one
// pixel short of the tick labels' offset so it never touches a label.
qreal cornerTickLength(const ChartStyle& style) {
    return qMin(style.tickLength, style.tickLabelOffset - 1);
}

int titleStripWidth(const QFontMetrics& fm) {
    return fm.height() + 4;
}

int widestTickLabel(const QFontMetrics& fm, const ValueScale& scale) {
    int width = 0;
    for (double tick : scale.ticks) {
        width = qMax(width, fm.horizontalAdvance(formatTick(tick, scale.decimals)));
    }
    return width;
}

int valueAxisWidth(const QFontMetrics& fm, const ChartValueAxis& axis, bool showTitle,
                   const ChartStyle& style) {
    const int titlePart =
        showTitle && !axis.title.isEmpty() ? titleStripWidth(fm) + kChartAxisLabelGap : 0;
    return titlePart + axis.labelWidth + yLabelOffset(style);
}

int xAxisBandHeight(const QFontMetrics& fm, const QFontMetrics& tickFm,
                    const CartesianChrome& chrome, const ChartStyle& style) {
    int band = 0;
    if (chrome.xTickLabels) {
        band += style.tickLabelOffset + tickFm.height();
    }
    if (chrome.xTitle) {
        band += kChartAxisLabelGap + fm.height();
    }
    return band;
}

void paintValueAxis(QPainter& painter, const QRect& plot, const ChartValueAxis& axis,
                    const CartesianChrome& chrome, const ChartStyle& style,
                    const ChartColors& colors, const QFont& baseFont, const QFont& tickFont,
                    QVector<QRect>* occupied) {
    const bool drawTicks = style.frame != ChartFrame::Ruler || chrome.showGrid;
    const bool frameIsSpine = axis.primary && style.frame != ChartFrame::Ruler;
    const qreal spineX = crispCoord(axis.spineX);

    painter.setPen(QPen(colors.frame, style.frameWidth));
    if (drawTicks && !frameIsSpine) {
        painter.drawLine(QPointF(spineX, plot.top()), QPointF(spineX, plot.bottom()));
    }
    if (drawTicks) {

        const bool mirror =
            axis.primary && style.frame == ChartFrame::Box && style.mirrorTicks;
        const qreal rightX = crispCoord(plot.right());
        QVector<qreal> ys;
        for (double tick : axis.scale.ticks) {
            ys.append(plotCrispY(plot, chartValueToY(plot, axis.scale, tick)));
        }
        if (frameIsSpine) {
            const qreal bottom = crispCoord(plot.bottom());
            painter.drawLine(QPointF(spineX, bottom),
                             QPointF(spineX - cornerTickLength(style), bottom));
        }
        for (qreal y : ys) {
            const qreal end = spineX - style.tickLength;
            painter.drawLine(QPointF(spineX, y), QPointF(end, y));
            if (mirror) {
                painter.drawLine(QPointF(rightX, y), QPointF(rightX - style.tickLength, y));
            }
        }
    }

    const QColor labelColor = axisLabelColor(axis, colors);
    const int labelRight = axis.spineX - yLabelOffset(style);
    if (chrome.yTickLabels) {
        painter.setFont(tickFont);
        painter.setPen(labelColor);
        const QFontMetrics tickFm(tickFont);
        for (double tick : axis.scale.ticks) {
            const int y = qRound(chartValueToY(plot, axis.scale, tick));
            const QRect rect(labelRight - axis.labelWidth, y - tickFm.height() / 2,
                             axis.labelWidth, tickFm.height());
            painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter,
                             formatTick(tick, axis.scale.decimals));
            if (occupied) {
                occupied->append(rect);
            }
        }
        painter.setFont(baseFont);
    }

    // The title (usually the unit) as its own vertical label in a strip left
    // of the tick labels, read bottom-to-top like a conventional axis title.
    // A stacked axis keeps its series color so it still matches its line.
    if (chrome.yTitle && !axis.title.isEmpty()) {
        const QFontMetrics fm(baseFont);
        const int stripWidth = titleStripWidth(fm);
        const int stripCenterX =
            labelRight - axis.labelWidth - kChartAxisLabelGap - stripWidth / 2;
        painter.save();
        painter.setPen(axis.labelColor.isValid() ? axis.labelColor : colors.axisTitle);
        painter.translate(stripCenterX, plot.center().y());
        painter.rotate(-90);
        const int textWidth = fm.horizontalAdvance(axis.title);
        painter.drawText(QRect(-textWidth / 2, -fm.height() / 2, textWidth, fm.height()),
                         Qt::AlignCenter, axis.title);
        painter.restore();
    }
}

void paintXTickMarks(QPainter& painter, const QRect& plot, const QVector<qreal>& xs,
                     const ChartStyle& style, const ChartColors& colors) {
    painter.setPen(QPen(colors.frame, style.frameWidth));
    const bool inward = style.tickDirection == ChartTickDirection::Inward;
    const bool mirror = style.frame == ChartFrame::Box && style.mirrorTicks;
    const qreal bottom = crispCoord(plot.bottom());
    const qreal top = crispCoord(plot.top());
    const qreal left = crispCoord(plot.left());
    painter.drawLine(QPointF(left, bottom), QPointF(left, bottom + cornerTickLength(style)));
    for (qreal x : xs) {
        const qreal cx = plotCrispX(plot, x);
        const qreal end = inward ? bottom - style.tickLength : bottom + style.tickLength;
        painter.drawLine(QPointF(cx, bottom), QPointF(cx, end));
        if (mirror) {
            painter.drawLine(QPointF(cx, top), QPointF(cx, top + style.tickLength));
        }
    }
}

}  // namespace

void paintChartBackground(QPainter& painter, const DashboardWidget& widget,
                          const ThemePalette& palette) {
    painter.fillPath(widget.contentFillPath(), palette.surface);
    if (!ThemeManager::instance().currentFrameStyle().idleOutline) {
        return;  // Borderless frame: the fill alone marks the widget
    }

    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    // Inset by 0.5 so the 1px pen renders at full strength instead of half
    // of it landing outside this widget's own paint device and getting
    // clipped -- same idea as DashboardCell's own outline stroke inset,
    // just for this widget's separate, decorative inner border.
    const QRectF strokeRect = QRectF(widget.rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.drawPath(widget.roundedPath(strokeRect));
}

QFont chartTickFont(const QFont& base, const ChartStyle& style) {
    QFont font = base;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    if (style.tabularFigures) {
        font.setFeature(QFont::Tag("tnum"), 1);
    }
#else
    Q_UNUSED(style);
#endif
    return font;
}

QFont chartTabularFont(const QFont& base) {
    QFont font = base;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.setFeature(QFont::Tag("tnum"), 1);
#endif
    return font;
}

int chartOuterPadding() {
    return ThemeManager::instance().currentDensity().contentPadding;
}

qreal crispCoord(qreal coord) {
    return qFloor(coord) + 0.5;
}

Qt::PenStyle chartPenStyle(ChartSeriesStyle style) {
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

bool isChartMarkerStyle(ChartSeriesStyle style) {
    return style == ChartSeriesStyle::Cross || style == ChartSeriesStyle::Asterisk;
}

void paintChartMarker(QPainter& painter, const QPointF& center, ChartSeriesStyle style,
                      qreal size) {
    painter.drawLine(QPointF(center.x() - size, center.y()),
                     QPointF(center.x() + size, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - size),
                     QPointF(center.x(), center.y() + size));
    if (style == ChartSeriesStyle::Asterisk) {
        const qreal d = size * 0.7;
        painter.drawLine(QPointF(center.x() - d, center.y() - d),
                         QPointF(center.x() + d, center.y() + d));
        painter.drawLine(QPointF(center.x() - d, center.y() + d),
                         QPointF(center.x() + d, center.y() - d));
    }
}

int chartLegendRowHeight(const QFontMetrics& fm) {
    return qMax(kLegendDotSize, fm.height());
}

int chartLegendSwatchWidth(const ChartStyle& style) {
    return style.legendSwatch == ChartLegendSwatch::LineSample ? kLegendLineSampleWidth
                                                               : kLegendDotSize;
}

QVector<ChartLegendColumn> chartLegendColumns(const QFontMetrics& fm, int left, int rightBound,
                                              int entryCount, const QVector<QStringList>& rows,
                                              const ChartStyle& style) {
    int textWidth = 0;
    for (const QStringList& row : rows) {
        for (int i = 0; i < entryCount && i < row.size(); ++i) {
            textWidth = qMax(textWidth, fm.horizontalAdvance(row[i]));
        }
    }
    textWidth += 1;  // an integer advance can run a fraction short on a scaled display
    const int columnWidth = chartLegendSwatchWidth(style) + kChartSwatchTextGap + textWidth;

    QVector<ChartLegendColumn> columns;
    int x = left;
    for (int i = 0; i < entryCount; ++i) {
        if (x + columnWidth > rightBound) {
            break;
        }
        columns.append({x, columnWidth});
        x += columnWidth + kChartLegendItemGap;
    }
    return columns;
}

void paintChartLegendSwatch(QPainter& painter, const QRect& slot, const QColor& color,
                            ChartSeriesStyle lineStyle, const ChartStyle& style) {
    const qreal centerY = slot.top() + slot.height() / 2.0;
    if (style.legendSwatch == ChartLegendSwatch::Dot) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(slot.left(), centerY - kLegendDotSize / 2.0, kLegendDotSize,
                                   kLegendDotSize));
        return;
    }
    const qreal left = slot.left();
    const qreal right = slot.left() + kLegendLineSampleWidth;
    if (isChartMarkerStyle(lineStyle)) {
        painter.setPen(QPen(color, 1.5));
        paintChartMarker(painter, QPointF((left + right) / 2.0, centerY), lineStyle, 4.0);
        return;
    }
    QPen pen(color, qMax<qreal>(2.0, style.lineWidth), chartPenStyle(lineStyle), Qt::FlatCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(left, centerY), QPointF(right, centerY));
}

void paintChartLegendRow(QPainter& painter, int y, int rowHeight,
                         const QVector<ChartLegendColumn>& columns,
                         const QVector<ChartLegendEntry>& entries, const QStringList& texts,
                         const ChartStyle& style, const ChartColors& colors,
                         const ThemePalette& palette) {
    const int swatchWidth = chartLegendSwatchWidth(style);
    for (int i = 0; i < columns.size() && i < entries.size() && i < texts.size(); ++i) {
        const int x = columns[i].x;
        const ChartLegendEntry& entry = entries[i];
        painter.setOpacity(entry.hidden ? 0.4 : 1.0);

        paintChartLegendSwatch(painter, QRect(x, y, swatchWidth, rowHeight),
                               entry.hidden ? palette.textSecondary : entry.color,
                               entry.lineStyle, style);

        painter.setPen(colors.legendText);
        const int textX = x + swatchWidth + kChartSwatchTextGap;
        painter.drawText(
            QRect(textX, y, columns[i].width - (swatchWidth + kChartSwatchTextGap), rowHeight),
            Qt::AlignLeft | Qt::AlignVCenter, texts[i]);
    }
    painter.setOpacity(1.0);
}

QVector<QRect> paintChartInsetLegend(QPainter& painter, const QRect& plotRect,
                                     ChartLegendPlacement placement,
                                     const QVector<ChartLegendEntry>& entries,
                                     const QStringList& texts, int opacityPercent,
                                     const ChartStyle& style, const ChartColors& colors,
                                     const ThemePalette& palette) {
    QVector<QRect> hitRects(entries.size());
    const QFontMetrics fm(painter.font());
    const int rowHeight = chartLegendRowHeight(fm);
    const int swatchWidth = chartLegendSwatchWidth(style);
    const int available = plotRect.height() - 2 * (kInsetLegendMargin + kInsetLegendPadding);
    const int rowCount = qMin<int>(qMin(entries.size(), texts.size()),
                                   (available + kInsetLegendRowGap) /
                                       (rowHeight + kInsetLegendRowGap));
    if (rowCount <= 0) {
        return hitRects;
    }

    // Measured in fractional pixels and rounded up, plus slack: an integer
    // advance can come out a fraction short of what is actually drawn on a
    // scaled display, which made the elide below cut perfectly fitting text.
    const QFontMetricsF fmf(painter.font());
    qreal textWidthF = 0.0;
    for (int i = 0; i < rowCount; ++i) {
        textWidthF = qMax(textWidthF, fmf.horizontalAdvance(texts[i]));
    }
    const int textWidth = qCeil(textWidthF) + 2;
    const int maxBoxWidth = plotRect.width() - 2 * kInsetLegendMargin;
    const int boxWidth = qMin(maxBoxWidth, 2 * kInsetLegendPadding + swatchWidth +
                                               kChartSwatchTextGap + textWidth);
    const int boxHeight = 2 * kInsetLegendPadding + rowCount * rowHeight +
                          (rowCount - 1) * kInsetLegendRowGap;
    if (boxWidth <= 2 * kInsetLegendPadding + swatchWidth) {
        return hitRects;
    }

    const bool left = placement == ChartLegendPlacement::TopLeft ||
                      placement == ChartLegendPlacement::BottomLeft;
    const bool top = placement == ChartLegendPlacement::TopLeft ||
                     placement == ChartLegendPlacement::TopRight;
    const int x = left ? plotRect.left() + kInsetLegendMargin
                       : plotRect.right() - kInsetLegendMargin - boxWidth + 1;
    const int y = top ? plotRect.top() + kInsetLegendMargin
                      : plotRect.bottom() - kInsetLegendMargin - boxHeight + 1;

    const double alpha = qBound(0, opacityPercent, 100) / 100.0;
    QColor background = palette.surface;
    background.setAlphaF(float(alpha));
    QColor outline = colors.frame;
    outline.setAlphaF(float(alpha));
    painter.setPen(alpha > 0.0 ? QPen(outline, 1) : QPen(Qt::NoPen));
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(crispCoord(x), crispCoord(y), boxWidth - 1, boxHeight - 1), 3,
                            3);
    painter.setBrush(Qt::NoBrush);

    const int columnWidth = boxWidth - 2 * kInsetLegendPadding;
    const int textRoom = columnWidth - swatchWidth - kChartSwatchTextGap;
    for (int i = 0; i < rowCount; ++i) {
        const int rowY = y + kInsetLegendPadding + i * (rowHeight + kInsetLegendRowGap);
        // Elided only when the plot is too narrow for the box.
        const QString text = fmf.horizontalAdvance(texts[i]) <= textRoom
                                 ? texts[i]
                                 : fmf.elidedText(texts[i], Qt::ElideRight, textRoom);
        paintChartLegendRow(painter, rowY, rowHeight, {{x + kInsetLegendPadding, columnWidth}},
                            {entries[i]}, {text}, style, colors, palette);
        hitRects[i] = QRect(x, rowY, boxWidth, rowHeight);
    }
    return hitRects;
}

ChartCartesianLayout layoutCartesianChart(const QPainter& painter, const QRect& area,
                                          const QVector<ChartAxisRequest>& yAxes,
                                          const CartesianChrome& chrome, const ChartStyle& style,
                                          const XScaleFactory& xScaleFor) {
    ChartCartesianLayout layout;
    layout.area = area;
    layout.hasXScale = bool(xScaleFor);
    layout.tickFont = chartTickFont(painter.font(), style);
    const QFontMetrics fm(painter.font());
    const QFontMetrics tickFm(layout.tickFont);

    CartesianChrome effective = chrome;
    if (!layout.hasXScale) {
        effective.xTickLabels = false;
        effective.xTitle = false;
    }

    // Top: one legend row (series names). Bottom: the X axis band, then one
    // more legend row (last values). Each is reserved whether or not there
    // is anything in it yet, so the plot doesn't jump when the first series
    // is added -- unless the chart asks for no such row at all.
    layout.legendRowHeight = chartLegendRowHeight(fm);
    const int rowSpace = layout.legendRowHeight + chartOuterPadding();
    // The top and bottom Y labels are centered on the plot's top and bottom
    // edges, so half of each sticks out past the plot. With no legend row
    // (or X axis band) on that side to absorb it, reserve that half on top
    // of the padding -- otherwise the plot looks pushed against that edge
    // while the labeled sides look roomy.
    const int labelOverhang = chrome.yTickLabels ? tickFm.height() / 2 : 0;
    const int xBand = xAxisBandHeight(fm, tickFm, effective, style);
    // Info rows sit close together (one block), then the usual padding.
    layout.infoRowPitch = layout.legendRowHeight + kInfoRowGap;
    const int infoBlock =
        chrome.infoRows > 0
            ? chrome.infoRows * layout.infoRowPitch - kInfoRowGap + chartOuterPadding()
            : 0;
    const int topMargin =
        chartOuterPadding() + infoBlock +
        (chrome.topLegendRow ? rowSpace : (infoBlock > 0 ? 0 : labelOverhang + kChartAxisLabelGap));
    layout.infoRowTop = area.top() + chartOuterPadding();
    layout.topLegendTop = layout.infoRowTop + infoBlock;
    const int bottomMargin = chartOuterPadding() + (chrome.bottomLegendRow ? rowSpace : 0) +
                             (xBand > 0 || chrome.bottomLegendRow ? xBand : labelOverhang);
    layout.bottomLegendTop = area.bottom() - chartOuterPadding() - layout.legendRowHeight + 1;
    const int plotHeight = area.height() - topMargin - bottomMargin;

    const int maxYTicks = maxTicksFor(plotHeight, tickFm.height() * kYTickSpacingLines);
    int leftMargin = chartOuterPadding();
    for (int i = 0; i < yAxes.size(); ++i) {
        const ChartAxisRequest& request = yAxes[i];
        ChartValueAxis axis;
        axis.scale = valueScale(request.range, style, maxYTicks, chrome.gridDivisions,
                                request.configDecimals, chrome.yTickCount);
        axis.title = request.title;
        axis.labelColor = request.labelColor;
        axis.primary = (i == 0);
        axis.labelWidth = chrome.yTickLabels ? widestTickLabel(tickFm, axis.scale) : 0;
        leftMargin += valueAxisWidth(fm, axis, chrome.yTitle, style);
        if (i + 1 < yAxes.size()) {
            leftMargin += kStackedAxisGap;
        }
        layout.yAxes.append(axis);
    }

    // The right side has no axis chrome of its own: give it a little more
    // than the bare padding so it balances the labeled left side.
    int rightMargin = chartOuterPadding() + kChartAxisLabelGap;
    const int plotWidth = area.width() - leftMargin - rightMargin;
    if (layout.hasXScale) {
        // Two passes: a first guess at the tick budget, then the budget the
        // actual widest label of that guess allows.
        const int guessSpacing = tickFm.horizontalAdvance(QStringLiteral("-0000")) +
                                 tickFm.averageCharWidth() * 3;
        layout.xScale = xScaleFor(plotWidth, maxTicksFor(plotWidth, guessSpacing));
        if (effective.xTickLabels && !layout.xScale.ticks.isEmpty()) {
            const int spacing = widestTickLabel(tickFm, layout.xScale) + tickFm.height() * 2;
            layout.xScale = xScaleFor(plotWidth, maxTicksFor(plotWidth, spacing));
            // The newest tick sits on the right edge; its label is centered
            // there, so half of it needs room past the plot.
            if (!layout.xScale.ticks.isEmpty()) {
                const int lastWidth = tickFm.horizontalAdvance(
                    formatTick(layout.xScale.ticks.last(), layout.xScale.decimals));
                rightMargin = chartOuterPadding() + qMax(kChartAxisLabelGap, lastWidth / 2);
            }
        }
    }

    layout.plotRect = QRect(area.left() + leftMargin, area.top() + topMargin,
                            area.width() - leftMargin - rightMargin, plotHeight);

    // Stacked axes: the primary spine sits on the plot's left edge, each
    // further axis one axis-width further left.
    int cursor = layout.plotRect.left();
    for (ChartValueAxis& axis : layout.yAxes) {
        axis.spineX = cursor;
        cursor -= valueAxisWidth(fm, axis, chrome.yTitle, style) + kStackedAxisGap;
    }
    return layout;
}

qreal chartValueToY(const QRect& plotRect, const ValueScale& scale, double value) {
    const double range = (scale.hi - scale.lo) != 0.0 ? (scale.hi - scale.lo) : 1.0;
    return plotRect.bottom() - plotRect.height() * ((value - scale.lo) / range);
}

qreal chartValueToX(const QRect& plotRect, const ValueScale& scale, double value) {
    const double range = (scale.hi - scale.lo) != 0.0 ? (scale.hi - scale.lo) : 1.0;
    return plotRect.right() - plotRect.width() * ((scale.hi - value) / range);
}

QVector<qreal> chartXGridPixels(const ChartCartesianLayout& layout) {
    QVector<qreal> xs;
    if (!layout.hasXScale) {
        return xs;
    }
    const QRect& plot = layout.plotRect;
    for (double tick : layout.xScale.gridTicks) {
        const qreal x = chartValueToX(plot, layout.xScale, tick);
        if (x > plot.left() + 0.5 && x < plot.right() - 0.5) {
            xs.append(x);
        }
    }
    return xs;
}

void paintCartesianAxes(QPainter& painter, const ChartCartesianLayout& layout,
                        const CartesianChrome& chrome, const ChartStyle& style,
                        const ChartColors& colors, const QString& xTitle,
                        const QVector<qreal>& categoryXs, QVector<QRect>* labelRects) {
    const QRect& plot = layout.plotRect;
    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    painter.setBrush(Qt::NoBrush);
    const QFont baseFont = painter.font();

    // 1. Grid -- under everything else. Only the primary axis' rows: every
    // axis maps into the same plot height, so more rows would only add noise.
    if (chrome.showGrid) {
        painter.setPen(QPen(colors.grid, 1, style.gridPenStyle));
        if (!layout.yAxes.isEmpty()) {
            const ChartValueAxis& primary = layout.yAxes.first();
            for (double tick : primary.scale.gridTicks) {
                const qreal y = plotCrispY(plot, chartValueToY(plot, primary.scale, tick));
                painter.drawLine(QPointF(crispCoord(plot.left()), y),
                                 QPointF(crispCoord(plot.right()), y));
            }
        }
        for (qreal x : chartXGridPixels(layout)) {
            const qreal cx = crispCoord(x);
            painter.drawLine(QPointF(cx, crispCoord(plot.top())),
                             QPointF(cx, crispCoord(plot.bottom())));
        }
    }

    // 2. Frame.
    painter.setPen(QPen(colors.frame, style.frameWidth));
    const qreal left = crispCoord(plot.left());
    const qreal right = crispCoord(plot.right());
    const qreal top = crispCoord(plot.top());
    const qreal bottom = crispCoord(plot.bottom());
    if (style.frame == ChartFrame::Box) {
        painter.drawRect(QRectF(QPointF(left, top), QPointF(right, bottom)));
    } else if (style.frame == ChartFrame::Spines) {
        painter.drawLine(QPointF(left, top), QPointF(left, bottom));
        painter.drawLine(QPointF(left, bottom), QPointF(right, bottom));
    }

    // 3. Value axes.
    QVector<QRect> occupied;
    for (const ChartValueAxis& axis : layout.yAxes) {
        paintValueAxis(painter, plot, axis, chrome, style, colors, baseFont, layout.tickFont,
                       &occupied);
    }

    // 4. X axis: tick marks (framed styles only), labels, title.
    const QFontMetrics fm(baseFont);
    const QFontMetrics tickFm(layout.tickFont);
    if (layout.hasXScale) {
        QVector<qreal> xs;
        for (double tick : layout.xScale.ticks) {
            xs.append(chartValueToX(plot, layout.xScale, tick));
        }
        if (style.frame != ChartFrame::Ruler) {
            paintXTickMarks(painter, plot, xs, style, colors);
        }
        if (chrome.xTickLabels) {
            painter.setFont(layout.tickFont);
            painter.setPen(colors.tickLabel);
            const int labelTop = plot.bottom() + style.tickLabelOffset;
            int previousRight = std::numeric_limits<int>::min();
            for (int i = 0; i < xs.size(); ++i) {
                const QString text = formatTick(layout.xScale.ticks[i], layout.xScale.decimals);
                const int width = tickFm.horizontalAdvance(text);
                QRect rect(qRound(xs[i]) - width / 2, labelTop, width, tickFm.height());
                if (rect.right() > layout.area.right() - 2) {
                    rect.moveRight(layout.area.right() - 2);
                }
                if (rect.left() < layout.area.left() + 2) {
                    rect.moveLeft(layout.area.left() + 2);
                }
                // Skip a label that would collide with its neighbor or with
                // the bottom Y label in the corner.
                bool collides = rect.left() < previousRight + kChartAxisLabelGap;
                for (const QRect& taken : occupied) {
                    collides = collides || taken.intersects(rect);
                }
                if (collides) {
                    continue;
                }
                painter.drawText(rect, Qt::AlignCenter, text);
                previousRight = rect.right();
                occupied.append(rect);
            }
            painter.setFont(baseFont);
        }
        if (chrome.xTitle && !xTitle.isEmpty()) {
            const int titleTop = plot.bottom() + 1 +
                                 (chrome.xTickLabels ? style.tickLabelOffset + tickFm.height()
                                                     : 0) +
                                 kChartAxisLabelGap;
            painter.setPen(colors.axisTitle);
            painter.drawText(QRect(plot.left(), titleTop, plot.width(), fm.height()),
                             Qt::AlignHCenter | Qt::AlignVCenter, xTitle);
        }
    } else if (!categoryXs.isEmpty() && style.frame != ChartFrame::Ruler) {
        paintXTickMarks(painter, plot, categoryXs, style, colors);
    }
    if (labelRects) {
        *labelRects += occupied;
    }
}

void paintChartPixelGrid(QPainter& painter, const QRect& plotRect,
                         const QVector<ChartPixelTick>& xTicks,
                         const QVector<ChartPixelTick>& yTicks, const ChartStyle& style,
                         const ChartColors& colors) {
    painter.setPen(QPen(colors.grid, 1, style.gridPenStyle));
    painter.setBrush(Qt::NoBrush);
    const QRectF plot(plotRect);
    for (const ChartPixelTick& tick : xTicks) {
        if (tick.pos > plot.left() + 0.5 && tick.pos < plot.right() - 0.5) {
            const qreal x = crispCoord(tick.pos);
            painter.drawLine(QPointF(x, crispCoord(plot.top())),
                             QPointF(x, crispCoord(plot.bottom())));
        }
    }
    for (const ChartPixelTick& tick : yTicks) {
        if (tick.pos > plot.top() + 0.5 && tick.pos < plot.bottom() - 0.5) {
            const qreal y = crispCoord(tick.pos);
            painter.drawLine(QPointF(crispCoord(plot.left()), y),
                             QPointF(crispCoord(plot.right()), y));
        }
    }
}

void paintChartPixelXTicks(QPainter& painter, const ChartCartesianLayout& layout,
                           const CartesianChrome& chrome, const QVector<ChartPixelTick>& ticks,
                           const ChartStyle& style, const ChartColors& colors,
                           QVector<QRect>* occupied) {
    const QRect& plot = layout.plotRect;
    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }
    if (style.frame != ChartFrame::Ruler) {
        QVector<qreal> xs;
        for (const ChartPixelTick& tick : ticks) {
            xs.append(tick.pos);
        }
        paintXTickMarks(painter, plot, xs, style, colors);
    }
    if (!chrome.xTickLabels) {
        return;
    }
    const QFont baseFont = painter.font();
    painter.setFont(layout.tickFont);
    painter.setPen(colors.tickLabel);
    const QFontMetrics tickFm(layout.tickFont);
    const int labelTop = plot.bottom() + style.tickLabelOffset;
    int previousRight = std::numeric_limits<int>::min() / 2;
    for (const ChartPixelTick& tick : ticks) {
        const int width = tickFm.horizontalAdvance(tick.label);
        QRect rect(qRound(tick.pos) - width / 2, labelTop, width, tickFm.height());
        if (rect.right() > layout.area.right() - 2) {
            rect.moveRight(layout.area.right() - 2);
        }
        if (rect.left() < layout.area.left() + 2) {
            rect.moveLeft(layout.area.left() + 2);
        }
        bool collides = rect.left() < previousRight + kChartAxisLabelGap;
        for (int i = 0; occupied && i < occupied->size() && !collides; ++i) {
            collides = occupied->at(i).intersects(rect);
        }
        if (collides) {
            continue;
        }
        painter.drawText(rect, Qt::AlignCenter, tick.label);
        previousRight = rect.right();
        if (occupied) {
            occupied->append(rect);
        }
    }
    painter.setFont(baseFont);
}

void paintChartPixelYTicks(QPainter& painter, const ChartCartesianLayout& layout,
                           const CartesianChrome& chrome, const QVector<ChartPixelTick>& ticks,
                           const ChartStyle& style, const ChartColors& colors,
                           QVector<QRect>* occupied) {
    const QRect& plot = layout.plotRect;
    if (plot.width() <= 0 || plot.height() <= 0 || layout.yAxes.isEmpty()) {
        return;
    }
    const ChartValueAxis& axis = layout.yAxes.first();
    const qreal spineX = crispCoord(axis.spineX);
    if (style.frame != ChartFrame::Ruler || chrome.showGrid) {
        painter.setPen(QPen(colors.frame, style.frameWidth));
        const bool mirror = style.frame == ChartFrame::Box && style.mirrorTicks;
        const qreal rightX = crispCoord(plot.right());
        for (const ChartPixelTick& tick : ticks) {
            const qreal y = plotCrispY(plot, tick.pos);
            painter.drawLine(QPointF(spineX, y), QPointF(spineX - style.tickLength, y));
            if (mirror) {
                painter.drawLine(QPointF(rightX, y), QPointF(rightX - style.tickLength, y));
            }
        }
    }
    if (!chrome.yTickLabels) {
        return;
    }
    const QFont baseFont = painter.font();
    painter.setFont(layout.tickFont);
    painter.setPen(axisLabelColor(axis, colors));
    const QFontMetrics tickFm(layout.tickFont);
    const int labelRight = axis.spineX - yLabelOffset(style);
    QRect previous;
    for (const ChartPixelTick& tick : ticks) {
        const int width = qMax(axis.labelWidth, tickFm.horizontalAdvance(tick.label));
        const QRect rect(labelRight - width, qRound(tick.pos) - tickFm.height() / 2, width,
                         tickFm.height());
        bool collides = !previous.isNull() && previous.adjusted(0, -1, 0, 1).intersects(rect);
        for (int i = 0; occupied && i < occupied->size() && !collides; ++i) {
            collides = occupied->at(i).intersects(rect);
        }
        if (collides) {
            continue;
        }
        painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, tick.label);
        previous = rect;
        if (occupied) {
            occupied->append(rect);
        }
    }
    painter.setFont(baseFont);
}

int chartInfoLeadWidth(const QFontMetrics& fm, const QString& text) {
    return kLegendDotSize + kChartSwatchTextGap + fm.horizontalAdvance(text) + 1;
}

void paintChartInfoRow(QPainter& painter, const ChartCartesianLayout& layout, int row,
                       const QVector<ChartInfoField>& left, const QString& right,
                       const ChartColors& colors, const ChartInfoLead& lead) {
    const QFont baseFont = painter.font();
    const QFont font = chartTabularFont(baseFont);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const QString separator = QStringLiteral("  ·  ");
    const int top = layout.infoRowTop + row * layout.infoRowPitch;
    const int height = layout.legendRowHeight;
    const int leftEdge = layout.area.left() + chartOuterPadding();
    const int rightEdge = layout.area.right() - chartOuterPadding();

    int x = leftEdge;
    if (lead.width > 0) {
        if (lead.swatch.isValid()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(lead.swatch);
            painter.drawEllipse(QRectF(x, top + (height - kLegendDotSize) / 2.0, kLegendDotSize,
                                       kLegendDotSize));
            painter.setBrush(Qt::NoBrush);
        }
        painter.setPen(colors.legendText);
        const int textX = x + kLegendDotSize + kChartSwatchTextGap;
        painter.drawText(QRect(textX, top, lead.width - (textX - x), height),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         fm.elidedText(lead.text, Qt::ElideRight, lead.width - (textX - x)));
        x += lead.width + kChartLegendItemGap;
    }
    painter.setPen(colors.legendText);
    for (int i = 0; i < left.size(); ++i) {
        const ChartInfoField& field = left[i];
        if (i > 0) {
            painter.drawText(QRect(x, top, fm.horizontalAdvance(separator), height),
                             Qt::AlignLeft | Qt::AlignVCenter, separator);
            x += fm.horizontalAdvance(separator);
        }
        const int at = field.format.indexOf(QLatin1String("%1"));
        const QString prefix = at < 0 ? field.format : field.format.left(at);
        const QString suffix = at < 0 ? QString() : field.format.mid(at + 2);
        const int slot = at < 0 ? 0
                                : qMax(fm.horizontalAdvance(field.widestValue),
                                       fm.horizontalAdvance(field.value));
        painter.drawText(QRect(x, top, fm.horizontalAdvance(prefix) + 1, height),
                         Qt::AlignLeft | Qt::AlignVCenter, prefix);
        x += fm.horizontalAdvance(prefix);
        if (at >= 0) {
            painter.drawText(QRect(x, top, slot, height), Qt::AlignRight | Qt::AlignVCenter,
                             field.value);
            x += slot;
        }
        painter.drawText(QRect(x, top, fm.horizontalAdvance(suffix) + 1, height),
                         Qt::AlignLeft | Qt::AlignVCenter, suffix);
        x += fm.horizontalAdvance(suffix);
    }

    const int room = rightEdge - x - kChartLegendItemGap;
    if (!right.isEmpty() && room > 0) {
        painter.drawText(QRect(rightEdge - room + 1, top, room, height),
                         Qt::AlignRight | Qt::AlignVCenter,
                         fm.elidedText(right, Qt::ElideRight, room));
    }
    painter.setFont(baseFont);
}

void paintChartRangeMarkers(QPainter& painter, const QRect& plotRect, qreal xA, qreal xB,
                            const ThemePalette& palette) {
    QColor band = palette.accent;
    band.setAlphaF(0.08f);
    painter.fillRect(QRectF(QPointF(qMin(xA, xB), plotRect.top()),
                            QPointF(qMax(xA, xB), plotRect.bottom())),
                     band);
    const QFontMetrics fm(painter.font());
    const int handle = fm.height() + 2;
    for (int which = 0; which < 2; ++which) {
        const qreal x = crispCoord(which == 0 ? xA : xB);
        painter.setPen(QPen(palette.accent, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        const QRectF tab(x - handle / 2.0, plotRect.top(), handle, handle);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.accent);
        painter.drawRoundedRect(tab, 3, 3);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(palette.surface);
        painter.drawText(tab, Qt::AlignCenter,
                         which == 0 ? QStringLiteral("A") : QStringLiteral("B"));
    }
}

int chartRangeMarkerAt(const QRect& plotRect, qreal xA, qreal xB, const QPoint& pos) {
    constexpr int kGrab = 6;
    if (plotRect.isEmpty() || pos.y() < plotRect.top() - kGrab || pos.y() > plotRect.bottom()) {
        return -1;
    }
    const qreal da = qAbs(pos.x() - xA);
    const qreal db = qAbs(pos.x() - xB);
    if (qMin(da, db) > kGrab) {
        return -1;
    }
    return da <= db ? 0 : 1;
}

QString chartTimeAxisTitle(ChartXAxisMode mode) {
    return mode == ChartXAxisMode::Time ? QCoreApplication::translate("ChartWidgets", "Time (s)")
                                        : QCoreApplication::translate("ChartWidgets", "Samples");
}

}  // namespace traceview

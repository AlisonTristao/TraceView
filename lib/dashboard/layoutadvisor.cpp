#include "layoutadvisor.h"

#include <QJsonArray>
#include <QtMath>

namespace traceview {

namespace {

// Hints in one line each: min, ideal, max (0 = none), aspect range.
WidgetLayoutHints hints(QSizeF minSize, QSizeF idealSize, QSizeF maxSize, double minAspect,
                        double maxAspect, const char* note) {
    WidgetLayoutHints h;
    h.minSize = minSize;
    h.idealSize = idealSize;
    h.maxSize = maxSize;
    h.minAspect = minAspect;
    h.maxAspect = maxAspect;
    h.note = QString::fromLatin1(note);
    return h;
}

// The controls: small, no header, and allowed on top of a widget that
// accepts overlays.
WidgetLayoutHints controlHints(QSizeF minSize, QSizeF idealSize, QSizeF maxSize, double minAspect,
                               double maxAspect, const char* note) {
    WidgetLayoutHints h = hints(minSize, idealSize, maxSize, minAspect, maxAspect, note);
    h.canOverlay = true;
    return h;
}

QString gaugeShapeOf(const QJsonObject& config) {
    return config.value("view").toObject().value("gaugeShape").toString("arc");
}

WidgetLayoutHints gaugeHints(const QString& shape) {
    if (shape == QLatin1String("halfCircle")) {
        WidgetLayoutHints h = hints({200, 140}, {320, 220}, {0, 0}, 1.2, 2.6,
                                    "Half-circle gauge: wider than tall. Its top corners, beside "
                                    "the arch, are free for an LED or a small button.");
        h.acceptsOverlay = true;
        h.freeZones = {QRectF(0.0, 0.12, 0.2, 0.28), QRectF(0.8, 0.12, 0.2, 0.28)};
        h.keepClear = {QRectF(0.2, 0.12, 0.6, 0.88)};
        return h;
    }
    if (shape == QLatin1String("dial")) {
        return hints({170, 190}, {280, 300}, {0, 0}, 0.75, 1.35,
                     "Analog dial: a round gauge on a square plate; square cells, the plate "
                     "fills the card.");
    }
    if (shape == QLatin1String("vu")) {
        return hints({170, 140}, {360, 220}, {0, 0}, 1.1, 5.0,
                     "Analog VU meter: one backlit meter per series side by side, each about "
                     "4:3; give it more width per series.");
    }
    if (shape == QLatin1String("edgewise")) {
        return hints({90, 200}, {200, 300}, {0, 0}, 0.3, 2.5,
                     "Analog edgewise meter: one tall strip per series side by side, about "
                     "70 px wide each; tall cells.");
    }
    if (shape == QLatin1String("counter")) {
        return hints({180, 90}, {300, 140}, {0, 0}, 1.5, 7.0,
                     "Analog counter: digit drums, a row per series of about 60 px; wide and "
                     "short.");
    }
    if (shape == QLatin1String("meter")) {
        return hints({180, 170}, {300, 270}, {0, 0}, 0.95, 1.6,
                     "Analog meter: a 4:3 panel meter with its caption under it. Keep overlays "
                     "off it, the bezel fills the cell.");
    }
    if (shape == QLatin1String("bar") || shape == QLatin1String("segments")) {
        return hints({220, 100}, {360, 190}, {0, 0}, 1.4, 6.0,
                     "Bar/LED-bar gauge: one row per series, about 40 px each; wide and short.");
    }
    if (shape == QLatin1String("display")) {
        return hints({160, 120}, {260, 160}, {0, 0}, 1.2, 4.0,
                     "Digital display: one display per series, wide; digits grow with the cell.");
    }
    if (shape == QLatin1String("number")) {
        return hints({120, 90}, {220, 160}, {0, 0}, 0.8, 3.0,
                     "Number gauge: just the value, as large as fits.");
    }
    WidgetLayoutHints h = hints({160, 180}, {280, 300}, {0, 0}, 0.7, 1.4,
                                "Ring gauge: square. The corners outside the ring (bottom left "
                                "and right) are free for LEDs or small buttons.");
    h.acceptsOverlay = true;
    h.freeZones = {QRectF(0.0, 0.8, 0.2, 0.2), QRectF(0.8, 0.8, 0.2, 0.2)};
    h.keepClear = {QRectF(0.15, 0.08, 0.7, 0.92)};
    return h;
}

QRectF zoneInRect(const QRectF& zone, const QRectF& rect) {
    return QRectF(rect.left() + zone.left() * rect.width(), rect.top() + zone.top() * rect.height(),
                  zone.width() * rect.width(), zone.height() * rect.height());
}

bool containsWithin(const QRectF& outer, const QRectF& inner, qreal tolerance) {
    return outer.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(inner);
}

QRectF centeredResize(const QRectF& rect, const QSizeF& size, const QSizeF& canvas) {
    QRectF out(QPointF(0, 0), size);
    out.moveCenter(rect.center());
    if (out.left() < 0) {
        out.moveLeft(0);
    }
    if (out.top() < 0) {
        out.moveTop(0);
    }
    if (canvas.width() > 0 && out.right() > canvas.width()) {
        out.moveRight(canvas.width());
    }
    if (canvas.height() > 0 && out.bottom() > canvas.height()) {
        out.moveBottom(canvas.height());
    }
    return out;
}

QString label(const LayoutReviewItem& item) {
    return item.name.isEmpty() ? item.typeId : QStringLiteral("\"%1\"").arg(item.name);
}

QString px(double value) {
    return QString::number(qRound(value));
}

QJsonObject sizeJson(const QSizeF& size, const QSizeF& cellSize) {
    QJsonObject json{{"widthPx", qRound(size.width())}, {"heightPx", qRound(size.height())}};
    if (cellSize.width() > 0 && cellSize.height() > 0) {
        json["cols"] = qMax(1, qCeil(size.width() / cellSize.width() - 0.05));
        json["rows"] = qMax(1, qCeil(size.height() / cellSize.height() - 0.05));
    }
    return json;
}

QJsonArray zonesJson(const QVector<QRectF>& zones) {
    QJsonArray list;
    for (const QRectF& zone : zones) {
        list.append(QJsonObject{
            {"x", zone.x()}, {"y", zone.y()}, {"width", zone.width()}, {"height", zone.height()}});
    }
    return list;
}

}  // namespace

WidgetLayoutHints widgetLayoutHints(const QString& typeId, const QJsonObject& config) {
    if (typeId == QLatin1String("push_button")) {
        return controlHints({72, 36}, {132, 52}, {240, 80}, 1.2, 6.0,
                            "Push button: short and fairly wide, like a key. Put a row of them "
                            "side by side at the same height; it may sit on a widget's free "
                            "zone.");
    }
    if (typeId == QLatin1String("toggle_switch")) {
        return controlHints({96, 64}, {150, 72}, {640, 110}, 1.2, 9.0,
                            "Toggle switch: title over the switch, left-aligned, so it can share "
                            "a column with sliders; more height only adds empty space.");
    }
    if (typeId == QLatin1String("slider")) {
        return controlHints({160, 44}, {300, 72}, {640, 100}, 2.5, 12.0,
                            "Slider: wide and short; the track takes the width, the value box "
                            "sits at its right.");
    }
    if (typeId == QLatin1String("led")) {
        return controlHints({44, 44}, {80, 104}, {140, 170}, 0.5, 1.6,
                            "LED: small, about square (taller with a title and state text). A "
                            "row of LEDs reads as a status panel; one fits in a gauge's free "
                            "corner, best without a title there.");
    }
    if (typeId == QLatin1String("dummy_line")) {
        WidgetLayoutHints h = hints({300, 170}, {600, 320}, {0, 0}, 1.5, 5.0,
                                    "Line chart: wide. The newest samples are on the right and "
                                    "the legend rows on top and bottom; the upper left of the "
                                    "plot (the oldest data) is free for a small control.");
        h.acceptsOverlay = true;
        h.freeZones = {QRectF(0.1, 0.18, 0.28, 0.24)};
        h.keepClear = {QRectF(0.6, 0.0, 0.4, 1.0), QRectF(0.0, 0.0, 1.0, 0.16),
                       QRectF(0.0, 0.88, 1.0, 0.12)};
        return h;
    }
    if (typeId == QLatin1String("dummy_bar")) {
        return hints({220, 170}, {360, 320}, {0, 0}, 0.8, 3.0,
                     "Bar chart: about square to wide; bars can reach the top, so keep overlays "
                     "off it.");
    }
    if (typeId == QLatin1String("dummy_gauge")) {
        return gaugeHints(gaugeShapeOf(config));
    }
    if (typeId == QLatin1String("audio_spectrum")) {
        return hints({300, 170}, {560, 300}, {0, 0}, 1.4, 4.0, "Audio analyzer: wide.");
    }
    if (typeId == QLatin1String("scene")) {
        WidgetLayoutHints h = hints({240, 200}, {420, 340}, {0, 0}, 0.8, 2.0,
                                    "Animated scene: about square; its top-left corner is free.");
        h.acceptsOverlay = true;
        h.freeZones = {QRectF(0.0, 0.1, 0.24, 0.2)};
        return h;
    }
    if (typeId == QLatin1String("text_board")) {
        return hints({240, 160}, {420, 300}, {0, 0}, 0.6, 4.0,
                     "Text board: the text scales to fit, so any size works; keep it readable.");
    }
    if (typeId == QLatin1String("serial_monitor") || typeId == QLatin1String("robot_log") ||
        typeId == QLatin1String("chat")) {
        return hints({280, 150}, {480, 320}, {0, 0}, 0.6, 5.0,
                     "Text panel: needs room for several lines; tall or wide both work.");
    }
    return hints({120, 90}, {280, 220}, {0, 0}, 0.0, 0.0, "Any size.");
}

QJsonObject widgetLayoutHintsJson(const WidgetLayoutHints& hints, const QSizeF& cellSize) {
    QJsonObject json{
        {"min", sizeJson(hints.minSize, cellSize)},
        {"ideal", sizeJson(hints.idealSize, cellSize)},
        {"canOverlay", hints.canOverlay},
        {"acceptsOverlay", hints.acceptsOverlay},
        {"note", hints.note},
    };
    if (hints.maxSize.width() > 0 && hints.maxSize.height() > 0) {
        json["max"] = sizeJson(hints.maxSize, cellSize);
    }
    if (hints.minAspect > 0 || hints.maxAspect > 0) {
        json["aspect"] = QJsonObject{{"min", hints.minAspect}, {"max", hints.maxAspect}};
    }
    if (!hints.freeZones.isEmpty()) {
        json["freeZones"] = zonesJson(hints.freeZones);
    }
    if (!hints.keepClear.isEmpty()) {
        json["keepClear"] = zonesJson(hints.keepClear);
    }
    return json;
}

QString layoutSeverityString(LayoutIssue::Severity severity) {
    switch (severity) {
        case LayoutIssue::Severity::Info:
            return QStringLiteral("info");
        case LayoutIssue::Severity::Problem:
            return QStringLiteral("problem");
        case LayoutIssue::Severity::Warning:
            break;
    }
    return QStringLiteral("warning");
}

LayoutReview reviewLayout(const QVector<LayoutReviewItem>& items, const QSizeF& canvasSize,
                          const QSize& gridCells) {
    LayoutReview review;
    // Slack before a size counts as off: grid snapping moves edges a few px.
    constexpr qreal kSlack = 4.0;
    constexpr double kTooLargeFactor = 1.15;

    QVector<WidgetLayoutHints> allHints;
    for (const LayoutReviewItem& item : items) {
        allHints.append(widgetLayoutHints(item.typeId, item.config));
    }

    for (int i = 0; i < items.size(); ++i) {
        const LayoutReviewItem& item = items[i];
        const WidgetLayoutHints& h = allHints[i];
        const QRectF& r = item.rect;

        if (canvasSize.width() > 0 && canvasSize.height() > 0 &&
            !QRectF(QPointF(0, 0), canvasSize)
                 .adjusted(-kSlack, -kSlack, kSlack, kSlack)
                 .contains(r)) {
            review.issues.append({LayoutIssue::Severity::Problem, item.id, "offCanvas",
                                  QStringLiteral("%1 runs past the canvas.").arg(label(item)),
                                  centeredResize(r, r.size(), canvasSize)});
        }

        if (r.width() + kSlack < h.minSize.width() || r.height() + kSlack < h.minSize.height()) {
            const QSizeF fixed(qMax(r.width(), h.minSize.width()),
                               qMax(r.height(), h.minSize.height()));
            review.issues.append(
                {LayoutIssue::Severity::Problem, item.id, "tooSmall",
                 QStringLiteral("%1 is %2x%3 px, below its minimum %4x%5 (ideal %6x%7).")
                     .arg(label(item), px(r.width()), px(r.height()), px(h.minSize.width()),
                          px(h.minSize.height()), px(h.idealSize.width()),
                          px(h.idealSize.height())),
                 centeredResize(r, fixed, canvasSize)});
        } else if ((h.maxSize.width() > 0 && r.width() > h.maxSize.width() * kTooLargeFactor) ||
                   (h.maxSize.height() > 0 && r.height() > h.maxSize.height() * kTooLargeFactor)) {
            QSizeF fixed = r.size();
            if (h.maxSize.width() > 0) {
                fixed.setWidth(qMin(fixed.width(), h.idealSize.width()));
            }
            if (h.maxSize.height() > 0) {
                fixed.setHeight(qMin(fixed.height(), h.idealSize.height()));
            }
            review.issues.append(
                {LayoutIssue::Severity::Warning, item.id, "tooLarge",
                 QStringLiteral("%1 is %2x%3 px, past its maximum %4x%5: the extra room is "
                                "empty. Ideal %6x%7.")
                     .arg(label(item), px(r.width()), px(r.height()), px(h.maxSize.width()),
                          px(h.maxSize.height()), px(h.idealSize.width()),
                          px(h.idealSize.height())),
                 centeredResize(r, fixed, canvasSize)});
        }

        if (r.height() > 0 && (h.minAspect > 0 || h.maxAspect > 0)) {
            const double aspect = r.width() / r.height();
            if ((h.minAspect > 0 && aspect < h.minAspect * 0.9) ||
                (h.maxAspect > 0 && aspect > h.maxAspect * 1.1)) {
                const double target = qBound(h.minAspect > 0 ? h.minAspect : aspect, aspect,
                                             h.maxAspect > 0 ? h.maxAspect : aspect);
                // Keep the larger side, fix the other.
                const QSizeF fixed = aspect < target ? QSizeF(r.width(), r.width() / target)
                                                     : QSizeF(r.height() * target, r.height());
                review.issues.append(
                    {LayoutIssue::Severity::Warning, item.id, "aspect",
                     QStringLiteral("%1 has width/height %2; it reads best between %3 and %4.")
                         .arg(label(item), QString::number(aspect, 'f', 2),
                              QString::number(h.minAspect, 'f', 1),
                              QString::number(h.maxAspect, 'f', 1)),
                     centeredResize(r, fixed, canvasSize)});
            }
        }
    }

    // Overlaps: item j is above item i.
    for (int j = 0; j < items.size(); ++j) {
        for (int i = 0; i < j; ++i) {
            const QRectF inter = items[i].rect.intersected(items[j].rect);
            if (inter.width() <= kSlack || inter.height() <= kSlack) {
                continue;
            }
            const WidgetLayoutHints& below = allHints[i];
            const WidgetLayoutHints& above = allHints[j];
            bool inFreeZone = false;
            QRectF bestZone;
            for (const QRectF& zone : below.freeZones) {
                const QRectF zonePx = zoneInRect(zone, items[i].rect);
                if (containsWithin(zonePx, inter, kSlack)) {
                    inFreeZone = true;
                }
                if (bestZone.isNull() && zonePx.width() + kSlack >= above.minSize.width() &&
                    zonePx.height() + kSlack >= above.minSize.height()) {
                    bestZone = zonePx;
                }
            }
            if (above.canOverlay && below.acceptsOverlay && inFreeZone) {
                continue;
            }
            LayoutIssue issue{LayoutIssue::Severity::Problem, items[j].id, "covers", QString(),
                              QRectF()};
            if (above.canOverlay && below.acceptsOverlay) {
                issue.message = QStringLiteral(
                                    "%1 covers part of %2 that shows data; move it into one of "
                                    "%2's free zones.")
                                    .arg(label(items[j]), label(items[i]));
                if (!bestZone.isNull()) {
                    const QSizeF size(qMin(items[j].rect.width(), bestZone.width()),
                                      qMin(items[j].rect.height(), bestZone.height()));
                    QRectF moved(QPointF(0, 0), size);
                    moved.moveCenter(bestZone.center());
                    issue.suggestedRect = moved;
                }
            } else if (above.canOverlay) {
                issue.message = QStringLiteral("%1 sits on %2, which has no room for overlays.")
                                    .arg(label(items[j]), label(items[i]));
            } else {
                issue.message = QStringLiteral(
                                    "%1 overlaps %2; only small controls and LEDs "
                                    "may sit on another widget.")
                                    .arg(label(items[j]), label(items[i]));
            }
            review.issues.append(issue);
        }
    }

    // Neighbors line up exactly or clearly not at all: side by side, the
    // same top and bottom; stacked, the same left and right. Off by less
    // than about a grid cell reads as a mistake. Overlays (a widget on
    // another) are left out: they sit inside their host on purpose.
    const double cellW = gridCells.width() > 0 && canvasSize.width() > 0
                             ? canvasSize.width() / gridCells.width()
                             : 21.0;
    const double cellH = gridCells.height() > 0 && canvasSize.height() > 0
                             ? canvasSize.height() / gridCells.height()
                             : 18.0;
    const auto overlapsAnother = [&items](int index) {
        for (int k = 0; k < items.size(); ++k) {
            if (k == index) {
                continue;
            }
            const QRectF inter = items[index].rect.intersected(items[k].rect);
            if (inter.width() > 4.0 && inter.height() > 4.0) {
                return true;
            }
        }
        return false;
    };
    QVector<bool> overlay(items.size(), false);
    for (int i = 0; i < items.size(); ++i) {
        overlay[i] = overlapsAnother(i);
    }
    const auto almost = [](qreal delta, double cell) {
        return delta > 1.5 && delta <= 1.25 * cell;
    };
    for (int j = 0; j < items.size(); ++j) {
        if (overlay[j]) {
            continue;
        }
        for (int i = 0; i < j; ++i) {
            if (overlay[i]) {
                continue;
            }
            const QRectF& a = items[i].rect;
            const QRectF& b = items[j].rect;
            const qreal overlapY = qMin(a.bottom(), b.bottom()) - qMax(a.top(), b.top());
            const qreal overlapX = qMin(a.right(), b.right()) - qMax(a.left(), b.left());
            QRectF fixed = b;
            QString what;
            qreal off = 0.0;
            // Only peers: neighbors of about the same height share a row, of
            // about the same width a column. A column of three next to a
            // column of five never lines up row by row, on purpose.
            const bool rowPeers = qAbs(a.height() - b.height()) <= 1.25 * cellH;
            const bool columnPeers = qAbs(a.width() - b.width()) <= 1.25 * cellW;
            if (rowPeers && overlapY > 0.5 * qMin(a.height(), b.height())) {
                const qreal top = qAbs(a.top() - b.top());
                const qreal bottom = qAbs(a.bottom() - b.bottom());
                if (almost(top, cellH)) {
                    fixed.setTop(a.top());
                    off = qMax(off, top);
                }
                if (almost(bottom, cellH)) {
                    fixed.setBottom(a.bottom());
                    off = qMax(off, bottom);
                }
                what = QStringLiteral("top and bottom");
            } else if (columnPeers && overlapX > 0.5 * qMin(a.width(), b.width())) {
                const qreal left = qAbs(a.left() - b.left());
                const qreal right = qAbs(a.right() - b.right());
                if (almost(left, cellW)) {
                    fixed.setLeft(a.left());
                    off = qMax(off, left);
                }
                if (almost(right, cellW)) {
                    fixed.setRight(a.right());
                    off = qMax(off, right);
                }
                what = QStringLiteral("left and right edges");
            }
            if (off > 0.0) {
                review.issues.append(
                    {LayoutIssue::Severity::Warning, items[j].id, "alignment",
                     QStringLiteral("%1 almost lines up with %2 (%3 px off); give them the same "
                                    "%4.")
                         .arg(label(items[j]), label(items[i]), px(off), what),
                     fixed});
                break;
            }
        }
    }

    // Empty areas: grid cells no widget covers. Touching widgets already
    // get the grid's gutter between them, so a gap is room nothing uses.
    if (gridCells.width() > 0 && gridCells.height() > 0 && !items.isEmpty()) {
        const int cols = gridCells.width();
        const int rows = gridCells.height();
        QVector<bool> covered(cols * rows, false);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const QPointF centre((c + 0.5) * cellW, (r + 0.5) * cellH);
                for (const LayoutReviewItem& item : items) {
                    if (item.rect.contains(centre)) {
                        covered[r * cols + c] = true;
                        break;
                    }
                }
            }
        }
        QVector<bool> seen(cols * rows, false);
        for (int start = 0; start < cols * rows; ++start) {
            if (covered[start] || seen[start]) {
                continue;
            }
            // Flood fill one empty region (4-neighbors).
            QVector<int> stack{start};
            seen[start] = true;
            int count = 0;
            int minC = cols, maxC = -1, minR = rows, maxR = -1;
            while (!stack.isEmpty()) {
                const int cell = stack.takeLast();
                const int c = cell % cols;
                const int r = cell / cols;
                ++count;
                minC = qMin(minC, c);
                maxC = qMax(maxC, c);
                minR = qMin(minR, r);
                maxR = qMax(maxR, r);
                const int next[4][2] = {{c - 1, r}, {c + 1, r}, {c, r - 1}, {c, r + 1}};
                for (const auto& n : next) {
                    if (n[0] < 0 || n[0] >= cols || n[1] < 0 || n[1] >= rows) {
                        continue;
                    }
                    const int index = n[1] * cols + n[0];
                    if (!covered[index] && !seen[index]) {
                        seen[index] = true;
                        stack.append(index);
                    }
                }
            }
            if (count < 2) {
                continue;
            }
            const QRectF area(minC * cellW, minR * cellH, (maxC - minC + 1) * cellW,
                              (maxR - minR + 1) * cellH);
            review.issues.append(
                {LayoutIssue::Severity::Warning, QString(), "emptyArea",
                 QStringLiteral("Columns %1-%2, rows %3-%4 are empty (%5 cells). The grid "
                                "already leaves a gutter between touching widgets, so grow a "
                                "neighbor into this room or move widgets to close it.")
                     .arg(minC)
                     .arg(maxC + 1)
                     .arg(minR)
                     .arg(maxR + 1)
                     .arg(count),
                 area});
        }
    }

    // Free overlay spots nobody is using yet.
    for (int i = 0; i < items.size(); ++i) {
        for (const QRectF& zone : allHints[i].freeZones) {
            const QRectF spot = zoneInRect(zone, items[i].rect);
            bool used = false;
            for (int j = i + 1; j < items.size() && !used; ++j) {
                const QRectF inter = spot.intersected(items[j].rect);
                used = inter.width() > kSlack && inter.height() > kSlack;
            }
            if (!used) {
                review.overlaySpots.append({items[i].id, spot});
            }
        }
    }
    return review;
}

}  // namespace traceview

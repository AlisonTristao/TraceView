#include "loopdiagram.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include "theme/iconutils.h"
#include "traceview/thememanager.h"

namespace tracelab {

namespace {

constexpr qreal kMargin = 16;
constexpr qreal kBoxWidth = 180;
constexpr qreal kGap = 170;        // between two blocks: the lanes' labels
constexpr qreal kLaneStep = 34;
constexpr qreal kHeader = 26;      // a block's title strip
constexpr qreal kSubtitle = 18;
constexpr qreal kFooter = 24;      // a block's status line
constexpr qreal kPortRadius = 3.5;
constexpr qreal kRiserStep = 12;   // between two commands' risers
constexpr qreal kReturnStep = 26;  // between two commands' return lines
constexpr qreal kArrow = 7;
constexpr qreal kRing = 4;
constexpr qreal kCornerRadius = 6;
constexpr int kIconSize = 14;

QColor laneColor(const traceview::ThemePalette& palette, LoopDiagram::Signal::Kind kind) {
    switch (kind) {
        case LoopDiagram::Signal::Measurement:
            return palette.accent;
        case LoopDiagram::Signal::Created:
            return palette.success;
        case LoopDiagram::Signal::Command:
            return palette.warning;
        case LoopDiagram::Signal::Preview:
            break;
    }
    return palette.textSecondary;
}

QColor toneColor(const traceview::ThemePalette& palette, int tone) {
    const QColor tones[] = {palette.textSecondary, palette.success, palette.warning, palette.danger};
    return tones[qBound(0, tone, 3)];
}

void drawArrowHead(QPainter* painter, const QPointF& tip, const QPointF& direction,
                   const QColor& color) {
    const QPointF normal(-direction.y(), direction.x());
    const QPointF back = tip - direction * kArrow;
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(QPolygonF({tip, back + normal * (kArrow * 0.5),
                                    back - normal * (kArrow * 0.5)}));
    painter->restore();
}

QFont scaledFont(QFont font, double factor, bool bold = false) {
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * factor));
    }
    font.setBold(bold);
    return font;
}

}  // namespace

LoopDiagram::LoopDiagram(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            qOverload<>(&QWidget::update));
    relayout();
}

void LoopDiagram::setModel(const Model& model) {
    m_model = model;
    const QSize before = m_size;
    relayout();
    if (m_size != before) {
        updateGeometry();
        resize(m_size);
    }
    update();
}

QRectF LoopDiagram::blockRect(int index) const {
    return m_blockRects.value(index);
}

QSize LoopDiagram::sizeHint() const {
    return m_size;
}

QSize LoopDiagram::minimumSizeHint() const {
    return m_size;
}

int LoopDiagram::laneOf(const QString& name) const {
    return m_laneByName.value(name, -1);
}

bool LoopDiagram::blockTouches(int block, int lane) const {
    const Block& b = m_model.blocks[block];
    const QString& name = m_model.signalList[m_lanes[lane].signal].name;
    return b.reads.contains(name) || b.writes.contains(name);
}

void LoopDiagram::relayout() {
    m_laneByName.clear();
    m_lanes.clear();
    m_commands.clear();
    m_blockRects.clear();
    const int blockCount = m_model.blocks.size();

    // The lanes: every signal but the commands, top to bottom.
    const qreal firstLaneY = kMargin + kHeader + kSubtitle + kLaneStep / 2;
    QVector<int> commandSignals;
    for (int i = 0; i < m_model.signalList.size(); ++i) {
        if (m_model.signalList[i].kind == Signal::Command) {
            commandSignals.append(i);
            continue;
        }
        Lane lane;
        lane.signal = i;
        lane.y = firstLaneY + m_lanes.size() * kLaneStep;
        m_laneByName.insert(m_model.signalList[i].name, m_lanes.size());
        m_lanes.append(lane);
    }
    const int commandCount = commandSignals.size();
    const qreal leftPad = commandCount > 0 ? 24 + kRiserStep * commandCount : 0;
    const auto columnX = [&](int column) {
        return kMargin + leftPad + column * (kBoxWidth + kGap);
    };

    // The blocks: as tall as the lanes they touch, plus their command ports.
    qreal bottom = firstLaneY;
    for (int b = 0; b < blockCount; ++b) {
        const Block& block = m_model.blocks[b];
        int first = -1;
        int last = -1;
        int commandPorts = 0;
        for (const QStringList* names : {&block.reads, &block.writes}) {
            for (const QString& name : *names) {
                const int lane = laneOf(name);
                if (lane >= 0) {
                    first = first < 0 ? lane : qMin(first, lane);
                    last = qMax(last, lane);
                } else {
                    ++commandPorts;
                }
            }
        }
        if (first < 0) {
            first = last = 0;
        }
        const qreal top = firstLaneY + first * kLaneStep - kLaneStep / 2 - kHeader - kSubtitle;
        qreal height = (last - first + 1) * kLaneStep + kHeader + kSubtitle + kFooter;
        height = qMax(height, kHeader + kSubtitle + kFooter + commandPorts * 22.0 + 8);
        m_blockRects.append(QRectF(columnX(b), top, kBoxWidth, height));
        bottom = qMax(bottom, top + height);
    }

    // Where each lane starts and runs to.
    const bool closed = blockCount > 1 && (m_model.blocks.last().kind == Block::Controller ||
                                           m_model.blocks.last().kind == Block::NoController);
    for (Lane& lane : m_lanes) {
        const QString& name = m_model.signalList[lane.signal].name;
        lane.producer = 0;
        for (int b = 0; b < blockCount; ++b) {
            if (m_model.blocks[b].writes.contains(name)) {
                lane.producer = b;
                break;
            }
        }
        lane.end = lane.producer;
        for (int b = lane.producer + 1; b < blockCount; ++b) {
            if (m_model.blocks[b].reads.contains(name)) {
                lane.end = b;
            }
        }
        if (closed) {
            lane.end = blockCount - 1;
        }
    }

    // The commands: from the block that writes them back to the plant.
    int writer = -1;
    for (int b = blockCount - 1; b > 0; --b) {
        if (m_model.blocks[b].kind == Block::Controller) {
            writer = b;
            break;
        }
    }
    const auto portY = [&](int block, const QStringList& names, const QString& name) {
        QStringList commands;
        for (const QString& n : names) {
            if (laneOf(n) < 0) {
                commands.append(n);
            }
        }
        const QRectF r = m_blockRects.value(block);
        const qreal bodyTop = r.top() + kHeader + kSubtitle;
        const qreal bodyHeight = r.height() - kHeader - kSubtitle - kFooter;
        const int k = qMax(0, commands.indexOf(name));
        return bodyTop + (k + 0.5) * bodyHeight / qMax<qsizetype>(1, commands.size());
    };
    for (int k = 0; k < commandCount; ++k) {
        Command command;
        command.signal = commandSignals[k];
        const QString& name = m_model.signalList[command.signal].name;
        if (writer >= 0 && m_model.blocks[writer].writes.contains(name)) {
            command.from = writer;
            command.fromY = portY(writer, m_model.blocks[writer].writes, name);
            command.fromX = m_blockRects[writer].right() + 16 + (commandCount - 1 - k) * kRiserStep;
        }
        command.toY = blockCount > 0 ? portY(0, m_model.blocks[0].reads, name) : firstLaneY;
        // Nested, so no two cross: the first command (the top ports) takes
        // the outermost risers and the lowest line back.
        command.returnY = bottom + 30 + (commandCount - 1 - k) * kReturnStep;
        command.toX = (blockCount > 0 ? m_blockRects[0].left() : kMargin) - 16 -
                      (commandCount - 1 - k) * kRiserStep;
        m_commands.append(command);
    }

    qreal contentBottom = bottom;
    for (const Command& command : std::as_const(m_commands)) {
        contentBottom = qMax(contentBottom, command.returnY + 8);
    }
    m_legendY = contentBottom + 26;
    qreal right = blockCount > 0 ? m_blockRects.last().right() : kMargin + kBoxWidth;
    right += commandCount > 0 ? 24 + kRiserStep * commandCount : (closed ? 0 : kGap * 0.6);
    const qreal legendWidth = 640;
    m_size = QSize(int(qMax(right, kMargin + legendWidth) + kMargin), int(m_legendY + 14 + kMargin));
}

QString LoopDiagram::signalLabel(const Signal& signal) const {
    return signal.count > 1 ? QStringLiteral("%1[%2]").arg(signal.name).arg(signal.count)
                            : signal.name;
}

void LoopDiagram::paintEvent(QPaintEvent*) {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QFont baseFont = font();
    const QFont smallFont = scaledFont(baseFont, 0.9);
    const QFont titleFont = scaledFont(baseFont, 1.0, true);
    const QFontMetricsF smallMetrics(smallFont);
    const int blockCount = m_model.blocks.size();
    const auto left = [&](int b) { return m_blockRects[b].left(); };
    const auto right = [&](int b) { return m_blockRects[b].right(); };
    const auto spans = [&](int b, qreal y) {
        const QRectF& r = m_blockRects[b];
        return y > r.top() + kHeader && y < r.bottom() - kFooter;
    };

    // The lanes, under the blocks.
    for (int l = 0; l < m_lanes.size(); ++l) {
        const Lane& lane = m_lanes[l];
        const Signal& signal = m_model.signalList[lane.signal];
        const QColor color = laneColor(palette, signal.kind);
        const Qt::PenStyle style = signal.kind == Signal::Preview ? Qt::DashLine : Qt::SolidLine;
        if (lane.producer >= blockCount) {
            continue;
        }
        bool modified = false;
        const auto pen = [&](bool thick) {
            QPen p(color, thick ? 2.6 : 1.6, style, Qt::FlatCap);
            return p;
        };
        if (lane.end == lane.producer) {
            // Nothing after its block: a stub that reads "not used".
            const qreal x0 = right(lane.producer);
            const qreal x1 = x0 + kGap * 0.6;
            painter.setPen(pen(false));
            painter.drawLine(QPointF(x0, lane.y), QPointF(x1 - kRing, lane.y));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(x1, lane.y), kRing, kRing);
            continue;
        }
        for (int c = lane.producer; c < lane.end; ++c) {
            if (c > lane.producer && m_model.blocks[c].writes.contains(signal.name)) {
                modified = true;
            }
            // The gap after block c, then through block c + 1 when the lane
            // goes on past it.
            const bool last = c + 1 == lane.end;
            const bool read = m_model.blocks[c + 1].reads.contains(signal.name);
            qreal x1 = left(c + 1);
            if (last && !read) {
                x1 -= 14;
            }
            painter.setPen(pen(modified));
            painter.drawLine(QPointF(right(c), lane.y), QPointF(x1 - (read ? kArrow : 0), lane.y));
            if (read) {
                drawArrowHead(&painter, QPointF(left(c + 1) - kPortRadius, lane.y), QPointF(1, 0),
                              color);
            } else if (last) {
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(QPointF(x1 + kRing, lane.y), kRing, kRing);
            }
            if (!last && !spans(c + 1, lane.y)) {
                painter.drawLine(QPointF(left(c + 1), lane.y), QPointF(right(c + 1), lane.y));
            }
        }
    }

    // The commands, back under the blocks to the plant.
    for (const Command& command : std::as_const(m_commands)) {
        const Signal& signal = m_model.signalList[command.signal];
        const QColor color = laneColor(palette, Signal::Command);
        painter.setPen(QPen(color, 1.6));
        painter.setBrush(Qt::NoBrush);
        const qreal plantLeft = blockCount > 0 ? left(0) : kMargin;
        QPainterPath path;
        if (command.from >= 0) {
            path.moveTo(right(command.from), command.fromY);
            path.lineTo(command.fromX, command.fromY);
            path.lineTo(command.fromX, command.returnY);
        } else {
            path.moveTo(command.toX + 80, command.returnY);
        }
        path.lineTo(command.toX, command.returnY);
        path.lineTo(command.toX, command.toY);
        path.lineTo(plantLeft - kArrow - kPortRadius, command.toY);
        if (command.from < 0) {
            painter.setPen(QPen(color, 1.6, Qt::DashLine));
        }
        painter.drawPath(path);
        drawArrowHead(&painter, QPointF(plantLeft - kPortRadius, command.toY), QPointF(1, 0), color);
        // Its name on the line back.
        painter.setFont(smallFont);
        painter.setPen(color);
        QString label = signalLabel(signal);
        if (!signal.unit.isEmpty()) {
            label += QStringLiteral(" [%1]").arg(signal.unit);
        }
        if (command.from < 0) {
            label += tr("  (nothing writes it)");
        }
        painter.drawText(QPointF(command.toX + 8, command.returnY - 4), label);
    }

    // The blocks.
    for (int b = 0; b < blockCount; ++b) {
        const Block& block = m_model.blocks[b];
        const QRectF r = m_blockRects[b];
        QPainterPath body;
        body.addRoundedRect(r, kCornerRadius, kCornerRadius);
        if (block.kind == Block::NoController) {
            painter.setPen(QPen(palette.borderStrong, 1.2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(body);
            painter.setFont(titleFont);
            painter.setPen(palette.textSecondary);
            painter.drawText(r.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                             block.title + QLatin1Char('\n') + block.subtitle);
            continue;
        }
        painter.setPen(QPen(block.selected ? palette.accent : palette.border, block.selected ? 2 : 1));
        painter.setBrush(palette.surface);
        painter.drawPath(body);
        painter.save();
        painter.setClipPath(body);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.surfaceAlt);
        painter.drawRect(QRectF(r.left(), r.top(), r.width(), kHeader));
        painter.restore();
        // Header: glyph, status dot, title.
        qreal x = r.left() + 8;
        if (!block.iconPath.isEmpty()) {
            const QPixmap icon = traceview::loadTintedIcon(block.iconPath, palette.textPrimary, kIconSize)
                                     .pixmap(kIconSize, kIconSize);
            painter.drawPixmap(QPointF(x, r.top() + (kHeader - kIconSize) / 2), icon);
            x += kIconSize + 6;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(toneColor(palette, block.tone));
        painter.drawEllipse(QPointF(x + 4, r.top() + kHeader / 2), 4, 4);
        x += 14;
        painter.setFont(titleFont);
        painter.setPen(palette.textPrimary);
        const QFontMetricsF titleMetrics(titleFont);
        painter.drawText(QRectF(x, r.top(), r.right() - x - 8, kHeader), Qt::AlignVCenter,
                         titleMetrics.elidedText(block.title, Qt::ElideRight, r.right() - x - 8));
        // What it is, and how it stands.
        painter.setFont(smallFont);
        painter.setPen(palette.textSecondary);
        painter.drawText(QRectF(r.left() + 8, r.top() + kHeader, r.width() - 16, kSubtitle),
                         Qt::AlignVCenter,
                         smallMetrics.elidedText(block.subtitle, Qt::ElideRight, r.width() - 16));
        painter.setPen(toneColor(palette, block.tone));
        painter.drawText(QRectF(r.left() + 8, r.bottom() - kFooter, r.width() - 16, kFooter),
                         Qt::AlignVCenter,
                         smallMetrics.elidedText(block.status, Qt::ElideRight, r.width() - 16));
    }

    // Over the blocks: lanes passing one by untouched, dashed.
    for (int l = 0; l < m_lanes.size(); ++l) {
        const Lane& lane = m_lanes[l];
        QColor color = laneColor(palette, m_model.signalList[lane.signal].kind);
        color.setAlphaF(0.55);
        painter.setPen(QPen(color, 1.2, Qt::DashLine));
        for (int c = lane.producer + 1; c < lane.end; ++c) {
            if (spans(c, lane.y) && !blockTouches(c, l)) {
                painter.drawLine(QPointF(left(c), lane.y), QPointF(right(c), lane.y));
            }
        }
    }

    // The ports, with their names inside the blocks.
    painter.setFont(smallFont);
    for (int b = 0; b < blockCount; ++b) {
        const Block& block = m_model.blocks[b];
        if (block.kind == Block::NoController) {
            continue;
        }
        const QRectF r = m_blockRects[b];
        const auto port = [&](const QString& name, bool output) {
            const int lane = laneOf(name);
            qreal y = 0;
            Signal::Kind kind = Signal::Command;
            int signalIndex = -1;
            if (lane >= 0) {
                y = m_lanes[lane].y;
                signalIndex = m_lanes[lane].signal;
                kind = m_model.signalList[signalIndex].kind;
            } else {
                for (const Command& command : std::as_const(m_commands)) {
                    if (m_model.signalList[command.signal].name == name) {
                        y = output ? command.fromY : command.toY;
                        signalIndex = command.signal;
                    }
                }
                if (signalIndex < 0) {
                    return;  // reads something no one gives it
                }
            }
            const QColor color = laneColor(palette, kind);
            const QPointF center(output ? r.right() : r.left(), y);
            painter.setPen(QPen(palette.surface, 1));
            painter.setBrush(color);
            painter.drawEllipse(center, kPortRadius, kPortRadius);
            painter.setPen(palette.textSecondary);
            const QString text = signalLabel(m_model.signalList[signalIndex]);
            const QRectF textRect = output ? QRectF(r.center().x(), y - 9, r.width() / 2 - 8, 18)
                                           : QRectF(r.left() + 8, y - 9, r.width() / 2 - 8, 18);
            painter.drawText(textRect, (output ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                             smallMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
        };
        for (const QString& name : block.reads) {
            port(name, false);
        }
        for (const QString& name : block.writes) {
            port(name, true);
        }
    }

    // The lanes' names and values in the gaps.
    for (const Lane& lane : std::as_const(m_lanes)) {
        const Signal& signal = m_model.signalList[lane.signal];
        const QColor color = laneColor(palette, signal.kind);
        if (lane.producer >= blockCount) {
            continue;
        }
        const int lastGap = qMax(lane.producer, lane.end - 1);
        bool modified = false;
        for (int c = lane.producer; c <= lastGap; ++c) {
            if (c > lane.producer && m_model.blocks[c].writes.contains(signal.name)) {
                modified = true;
            }
            QString text = signalLabel(signal);
            QString value;
            if (c == lane.producer && !signal.measured.isEmpty()) {
                value = signal.measured;
            } else if (c == lastGap && modified && !signal.final.isEmpty()) {
                value = signal.final;
            }
            if (!value.isEmpty()) {
                text += QStringLiteral("  ") + value;
                if (!signal.unit.isEmpty()) {
                    text += QLatin1Char(' ') + signal.unit;
                }
            } else if (c == lane.producer && !signal.unit.isEmpty()) {
                text += QStringLiteral(" [%1]").arg(signal.unit);
            }
            const qreal x0 = right(c) + 8;
            const qreal width = (c + 1 < blockCount ? left(c + 1) : x0 + kGap * 0.6) - x0 - 12;
            painter.setPen(c == lane.producer || !value.isEmpty() ? color : palette.textSecondary);
            painter.drawText(QRectF(x0, lane.y - 17, width, 15), Qt::AlignLeft | Qt::AlignBottom,
                             smallMetrics.elidedText(text, Qt::ElideRight, width));
        }
    }

    // The legend.
    struct Item {
        QColor color;
        QString text;
        int shape;  // 0 line, 1 thick line, 2 dashed, 3 ring
    };
    const Item items[] = {
        {laneColor(palette, Signal::Measurement), tr("plant output"), 0},
        {laneColor(palette, Signal::Created), tr("made by a filter"), 0},
        {laneColor(palette, Signal::Command), tr("plant input (command)"), 0},
        {palette.textSecondary, tr("changed by a filter"), 1},
        {palette.textSecondary, tr("passes by"), 2},
        {palette.textSecondary, tr("not read"), 3},
    };
    qreal x = kMargin;
    const qreal y = m_legendY;
    painter.setFont(smallFont);
    for (const Item& item : items) {
        painter.setBrush(Qt::NoBrush);
        if (item.shape == 3) {
            painter.setPen(QPen(item.color, 1.4));
            painter.drawEllipse(QPointF(x + 9, y), kRing, kRing);
        } else {
            painter.setPen(QPen(item.color, item.shape == 1 ? 2.6 : 1.6,
                                item.shape == 2 ? Qt::DashLine : Qt::SolidLine, Qt::FlatCap));
            painter.drawLine(QPointF(x, y), QPointF(x + 18, y));
        }
        painter.setPen(palette.textSecondary);
        painter.drawText(QPointF(x + 24, y + smallMetrics.ascent() / 2 - 1), item.text);
        x += 24 + smallMetrics.horizontalAdvance(item.text) + 18;
    }
}

QString LoopDiagram::laneTip(const QPointF& pos) const {
    const auto titles = [&](const QString& name, bool reads) {
        QStringList list;
        for (const Block& block : m_model.blocks) {
            if ((reads ? block.reads : block.writes).contains(name)) {
                list.append(block.title);
            }
        }
        return list.join(QStringLiteral(", "));
    };
    const auto describe = [&](const Signal& signal) {
        QString text = QStringLiteral("<b>%1</b>").arg(signalLabel(signal).toHtmlEscaped());
        if (!signal.unit.isEmpty()) {
            text += QStringLiteral(" [%1]").arg(signal.unit.toHtmlEscaped());
        }
        const QString writers = titles(signal.name, false);
        const QString readers = titles(signal.name, true);
        text += QStringLiteral("<br>") + tr("Written by: %1").arg(writers.isEmpty() ? tr("nobody") : writers);
        text += QStringLiteral("<br>") + tr("Read by: %1").arg(readers.isEmpty() ? tr("nobody") : readers);
        if (!signal.measured.isEmpty()) {
            text += QStringLiteral("<br>") + tr("From the plant: %1").arg(signal.measured);
        }
        if (!signal.final.isEmpty() && signal.final != signal.measured) {
            text += QStringLiteral("<br>") + tr("What the controller reads: %1").arg(signal.final);
        }
        return text;
    };
    for (const Lane& lane : m_lanes) {
        if (qAbs(pos.y() - lane.y) <= 6 && lane.producer < m_blockRects.size()) {
            return describe(m_model.signalList[lane.signal]);
        }
    }
    for (const Command& command : m_commands) {
        if (qAbs(pos.y() - command.returnY) <= 6) {
            return describe(m_model.signalList[command.signal]);
        }
    }
    return QString();
}

void LoopDiagram::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int b = 0; b < m_blockRects.size(); ++b) {
            if (m_blockRects[b].contains(event->position())) {
                emit blockClicked(b);
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void LoopDiagram::mouseMoveEvent(QMouseEvent* event) {
    bool overBlock = false;
    for (const QRectF& r : std::as_const(m_blockRects)) {
        overBlock = overBlock || r.contains(event->position());
    }
    setCursor(overBlock ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

bool LoopDiagram::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString tip = laneTip(help->pos());
        if (tip.isEmpty()) {
            QToolTip::hideText();
            event->ignore();
        } else {
            QToolTip::showText(help->globalPos(), tip, this);
        }
        return true;
    }
    return QWidget::event(event);
}

}  // namespace tracelab

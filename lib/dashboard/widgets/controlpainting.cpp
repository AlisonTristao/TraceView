#include "controlpainting.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <cmath>

namespace traceview {

namespace {

// The Panel look is hardware: bezels, nuts and fader caps are metal and
// black plastic whatever the palette, the way a real panel's knobs don't
// change color with the room. These are its only fixed colors.
const QColor kBezelOuter(0x2b, 0x2b, 0x2b);
const QColor kBezelEdge(0x4a, 0x4a, 0x4a);
const QColor kBezelInner(0x0e, 0x0e, 0x0e);
const QColor kLensRing(0x3a, 0x3a, 0x38);
const QColor kLensSocket(0x1f, 0x1f, 0x1e);
const QColor kMetal(0x9a, 0x9a, 0x9a);
const QColor kMetalEdge(0x55, 0x55, 0x55);
const QColor kMetalDark(0x3a, 0x3a, 0x3a);
const QColor kMetalLight(0xd0, 0xd0, 0xd0);
const QColor kMetalBall(0xf0, 0xf0, 0xf0);
const QColor kMetalBallEdge(0x88, 0x88, 0x88);
const QColor kFaderCap(0xc4, 0xc4, 0xc4);
const QColor kFaderGroove(0x8a, 0x8a, 0x8a);
const QColor kSlot(0x0a, 0x0a, 0x0a);
const QColor kSlotEdge(0x33, 0x33, 0x33);
const QColor kDisplay(0x05, 0x05, 0x05);
const QColor kPlateEdge(0x44, 0x44, 0x44);

constexpr qreal kDisabledOpacity = 0.45;

QColor withAlpha(QColor color, qreal alpha) {
    color.setAlphaF(float(qBound(0.0, alpha, 1.0)));
    return color;
}

// `r` with its top-left and bottom-right corners cut by `cut` (the Retro
// chamfer), or rounded by `cut` when `chamfer` is off.
QPainterPath cornerPath(const QRectF& r, qreal cut, bool chamfer) {
    QPainterPath path;
    if (!chamfer) {
        path.addRoundedRect(r, cut, cut);
        return path;
    }
    cut = qMin(cut, qMin(r.width(), r.height()) / 2.0);
    path.addPolygon(
        QPolygonF({QPointF(r.left() + cut, r.top()), QPointF(r.right(), r.top()),
                   QPointF(r.right(), r.bottom() - cut), QPointF(r.right() - cut, r.bottom()),
                   QPointF(r.left(), r.bottom()), QPointF(r.left(), r.top() + cut)}));
    path.closeSubpath();
    return path;
}

void drawCenteredText(QPainter& painter, const QRectF& rect, const QString& text,
                      const QColor& color) {
    painter.setPen(color);
    const QFontMetricsF metrics(painter.font());
    painter.drawText(rect, Qt::AlignCenter,
                     metrics.elidedText(text, Qt::ElideRight, qMax(0.0, rect.width() - 4.0)));
}

QFont fixedPitch(const QFont& base) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pixelSize() > 0) {
        font.setPixelSize(base.pixelSize());
    } else {
        font.setPointSizeF(base.pointSizeF());
    }
    font.setWeight(base.weight());
    return font;
}

QFont serif(const QFont& base) {
    QFont font = base;
    font.setFamilies({QStringLiteral("Georgia"), QStringLiteral("Times New Roman"),
                      QStringLiteral("Noto Serif"), QStringLiteral("DejaVu Serif")});
    font.setStyleHint(QFont::Serif);
    return font;
}

// The layered "real LED" of the Lens look, also the Lever's pilot lamp.
void paintLensLed(QPainter& painter, const QPointF& c, qreal r, const QColor& color, bool on) {
    painter.setPen(Qt::NoPen);
    if (on) {
        QRadialGradient halo(c, r * kControlLedReach);
        halo.setColorAt(0.55, withAlpha(color, 0.45));
        halo.setColorAt(1.0, withAlpha(color, 0.0));
        painter.setBrush(halo);
        painter.drawEllipse(c, r * kControlLedReach, r * kControlLedReach);
    }
    painter.setBrush(kLensRing);
    painter.drawEllipse(c, r * 1.08, r * 1.08);
    painter.setBrush(kLensSocket);
    painter.drawEllipse(c, r, r);
    painter.setBrush(on ? color : mixControlColor(color, Qt::black, 0.72));
    painter.drawEllipse(c, r * 0.9, r * 0.9);
    painter.setBrush(on ? mixControlColor(color, Qt::white, 0.35)
                        : mixControlColor(color, Qt::black, 0.62));
    painter.drawEllipse(c, r * 0.62, r * 0.62);

    painter.save();
    painter.translate(c.x() - r * 0.28, c.y() - r * 0.38);
    painter.rotate(-30.0);
    painter.setBrush(withAlpha(Qt::white, on ? 0.75 : 0.18));
    painter.drawEllipse(QPointF(0, 0), r * 0.3, r * 0.2);
    painter.restore();
    painter.setBrush(withAlpha(Qt::white, on ? 0.35 : 0.08));
    painter.drawEllipse(QPointF(c.x() + r * 0.24, c.y() + r * 0.28), r * 0.06, r * 0.06);
}

QString unlitDigits(const QString& text) {
    QString ghost = text;
    for (QChar& ch : ghost) {
        if (ch.isDigit()) {
            ch = QLatin1Char('8');
        }
    }
    return ghost;
}

}  // namespace

void paintPanelScrew(QPainter& painter, const QPointF& center, qreal radius) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // A fixed slot angle per position, so a row of screws doesn't look
    // stamped from one template -- the way real ones never line up.
    const qreal angle = std::fmod(center.x() * 7.0 + center.y() * 13.0, 180.0);
    QRadialGradient head(QPointF(center.x() - radius * 0.35, center.y() - radius * 0.35),
                         radius * 1.4);
    head.setColorAt(0.0, kMetalLight);
    head.setColorAt(1.0, kMetalEdge);
    painter.setPen(QPen(kMetalDark, 0.8));
    painter.setBrush(head);
    painter.drawEllipse(center, radius, radius);
    painter.translate(center);
    painter.rotate(angle);
    painter.setPen(QPen(kMetalDark, qMax(0.8, radius * 0.32), Qt::SolidLine, Qt::FlatCap));
    painter.drawLine(QPointF(-radius * 0.7, 0), QPointF(radius * 0.7, 0));
    painter.restore();
}

void paintPanelScrews(QPainter& painter, const QRectF& rect, qreal inset, qreal radius) {
    if (rect.width() < 4 * inset || rect.height() < 4 * inset) {
        return;
    }
    for (const QPointF& corner : {QPointF(rect.left() + inset, rect.top() + inset),
                                  QPointF(rect.right() - inset, rect.top() + inset),
                                  QPointF(rect.left() + inset, rect.bottom() - inset),
                                  QPointF(rect.right() - inset, rect.bottom() - inset)}) {
        paintPanelScrew(painter, corner, radius);
    }
}

QFont controlTextFont(const ControlStyle& style, const QFont& base) {
    if (style.monoText) {
        return fixedPitch(base);
    }
    if (style.serifText) {
        return serif(base);
    }
    return base;
}

QFont controlTitleFont(const ControlStyle& style, const QFont& base) {
    QFont font = style.monoText ? fixedPitch(base) : base;
    if (style.serifItalicTitle) {
        font = serif(font);
        font.setItalic(true);
    }
    if (style.titleLetterSpacing != 0.0) {
        font.setLetterSpacing(QFont::PercentageSpacing, 100.0 + style.titleLetterSpacing);
    }
    return font;
}

QColor controlTitleColor(const ControlStyle& style, const ControlColors& colors) {
    if (style.accentTitles) {
        return colors.accent;
    }
    return style.brightTitles ? colors.text : colors.textSecondary;
}

QColor controlStateTextColor(const ControlStyle& style, const ControlColors& colors) {
    return style.accentTitles ? colors.accent : colors.text;
}

void paintControlButton(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                        const ControlColors& colors, const QFont& font, const QString& text,
                        ControlTone tone, bool down, bool hovered, bool enabled) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!enabled) {
        painter.setOpacity(kDisabledOpacity);
    }
    QFont labelFont = font;
    labelFont.setWeight(QFont::Medium);
    painter.setFont(labelFont);
    const QColor toneColor = controlToneColor(colors, tone);
    const QString label = controlStyledText(style, text);
    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);

    switch (style.button) {
        case ControlButtonLook::Filled: {
            QColor fill = toneColor;
            if (down) {
                fill = mixControlColor(fill, Qt::black, 0.2);
            } else if (hovered) {
                fill = mixControlColor(fill, Qt::white, 0.1);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(cornerPath(rect, style.cornerRadius, style.chamfer));
            drawCenteredText(painter, rect, label, textOnColor(fill));
            break;
        }
        case ControlButtonLook::Striped: {
            QColor fill = colors.surfaceAlt;
            if (down) {
                fill = mixControlColor(fill, colors.text, 0.12);
            } else if (hovered) {
                fill = mixControlColor(fill, colors.text, 0.05);
            }
            painter.setPen(QPen(colors.frame, style.lineWidth));
            painter.setBrush(fill);
            painter.drawRoundedRect(r, style.cornerRadius, style.cornerRadius);
            if (!down) {
                painter.setPen(QPen(withAlpha(Qt::white, 0.35), 1.0));
                painter.drawLine(QPointF(r.left() + 2, r.top() + 1.5),
                                 QPointF(r.right() - 2, r.top() + 1.5));
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(toneColor);
            painter.drawRect(QRectF(r.left(), r.top(), 4.0, r.height()));
            const QRectF textRect = rect.adjusted(4.0, down ? 1.0 : 0.0, 0.0, down ? 1.0 : 0.0);
            drawCenteredText(painter, textRect, label,
                             tone == ControlTone::Accent ? colors.text : toneColor);
            break;
        }
        case ControlButtonLook::Outline: {
            const QColor ink = tone == ControlTone::Accent ? colors.text : toneColor;
            painter.setPen(QPen(ink, style.lineWidth));
            painter.setBrush(down      ? withAlpha(ink, 0.14)
                             : hovered ? withAlpha(ink, 0.06)
                                       : QColor(Qt::transparent));
            painter.drawRoundedRect(r, style.cornerRadius, style.cornerRadius);
            labelFont.setWeight(QFont::Normal);
            painter.setFont(labelFont);
            drawCenteredText(painter, rect, label, ink);
            break;
        }
        case ControlButtonLook::Arcade: {
            const QFontMetricsF metrics(labelFont);
            const qreal diameter =
                qMax(16.0, qMin(qMin(rect.height() - 6.0, rect.width() * 0.45), 72.0));
            const qreal gap = diameter * 0.3;
            QString shown = label;
            qreal textWidth = metrics.horizontalAdvance(shown);
            const qreal room = rect.width() - diameter - gap - 4.0;
            if (textWidth > room) {
                shown = metrics.elidedText(label, Qt::ElideRight, qMax(0.0, room));
                textWidth = shown.isEmpty() ? 0.0 : metrics.horizontalAdvance(shown);
            }
            const qreal groupWidth = diameter + (shown.isEmpty() ? 0.0 : gap + textWidth);
            const QPointF c(rect.center().x() - groupWidth / 2.0 + diameter / 2.0,
                            rect.center().y());
            const qreal outer = diameter / 2.0;
            const qreal press = down ? outer * 0.08 : 0.0;
            painter.setPen(Qt::NoPen);
            if (down) {
                QRadialGradient halo(c, outer * 1.3);
                halo.setColorAt(0.7, withAlpha(toneColor, 0.35));
                halo.setColorAt(1.0, withAlpha(toneColor, 0.0));
                painter.setBrush(halo);
                painter.drawEllipse(c, outer * 1.3, outer * 1.3);
            }
            painter.setPen(QPen(kBezelEdge, 1.0));
            painter.setBrush(kBezelOuter);
            painter.drawEllipse(c, outer, outer);
            painter.setPen(Qt::NoPen);
            painter.setBrush(kBezelInner);
            painter.drawEllipse(c, outer * 0.83, outer * 0.83);
            const QPointF cap(c.x(), c.y() + press);
            QColor capColor = down ? mixControlColor(toneColor, Qt::white, 0.15)
                                   : mixControlColor(toneColor, Qt::black, hovered ? 0.1 : 0.22);
            painter.setBrush(capColor);
            painter.drawEllipse(cap, outer * 0.67, outer * 0.67);
            painter.setBrush(withAlpha(Qt::white, 0.45));
            painter.drawEllipse(QPointF(cap.x() - outer * 0.22, cap.y() - outer * 0.28),
                                outer * 0.28, outer * 0.17);
            if (!shown.isEmpty()) {
                painter.setPen(colors.text);
                painter.drawText(
                    QRectF(c.x() + outer + gap, rect.top(), textWidth + 2.0, rect.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, shown);
            }
            break;
        }
        case ControlButtonLook::Bracket: {
            QColor fill = Qt::transparent;
            if (down) {
                fill = toneColor;
            } else if (hovered) {
                fill = withAlpha(toneColor, 0.15);
            }
            painter.setPen(QPen(toneColor, style.lineWidth));
            painter.setBrush(fill);
            painter.drawPath(cornerPath(r, style.cornerRadius, true));
            labelFont.setWeight(QFont::Normal);
            painter.setFont(labelFont);
            drawCenteredText(painter, rect, QStringLiteral("[ %1 ]").arg(label),
                             down ? colors.background : toneColor);
            break;
        }
    }
    painter.restore();
}

QSizeF controlSwitchSize(const ControlStyle& style) {
    switch (style.switchLook) {
        case ControlSwitchLook::Rocker:
            return {64.0, 26.0};
        case ControlSwitchLook::Checkbox:
            return {18.0, 18.0};
        case ControlSwitchLook::Lever:
            return {64.0, 44.0};
        case ControlSwitchLook::Cells:
            return {84.0, 24.0};
        case ControlSwitchLook::Pill:
            break;
    }
    return {44.0, 24.0};
}

void paintControlSwitch(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                        const ControlColors& colors, const QFont& font, qreal t, bool enabled,
                        const QString& offText, const QString& onText) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!enabled) {
        painter.setOpacity(kDisabledOpacity);
    }
    t = qBound(0.0, t, 1.0);
    const bool on = t >= 0.5;
    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);

    switch (style.switchLook) {
        case ControlSwitchLook::Pill: {
            const qreal radius = r.height() / 2.0;
            painter.setPen(QPen(colors.border, 1.0));
            painter.setBrush(mixControlColor(colors.surfaceAlt, colors.accent, t));
            painter.drawRoundedRect(r, radius, radius);
            constexpr qreal kThumbMargin = 3.0;
            const qreal thumb = r.height() - 2 * kThumbMargin;
            const qreal travel = r.width() - thumb - 2 * kThumbMargin;
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.background);
            painter.drawEllipse(
                QRectF(r.left() + kThumbMargin + travel * t, r.top() + kThumbMargin, thumb, thumb));
            break;
        }
        case ControlSwitchLook::Rocker: {
            painter.setPen(QPen(colors.frame, style.lineWidth));
            painter.setBrush(colors.surfaceAlt);
            painter.drawRoundedRect(r, style.cornerRadius, style.cornerRadius);
            const qreal half = r.width() / 2.0;
            const QRectF active(r.left() + half * t, r.top(), half, r.height());
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.accent);
            painter.drawRoundedRect(active, style.cornerRadius, style.cornerRadius);
            QFont glyphFont = fixedPitch(font);
            glyphFont.setWeight(QFont::Medium);
            painter.setFont(glyphFont);
            const QColor onAccent = textOnColor(colors.accent);
            drawCenteredText(painter, QRectF(r.left(), r.top(), half, r.height()),
                             QStringLiteral("0"), on ? colors.textSecondary : onAccent);
            drawCenteredText(painter, QRectF(r.left() + half, r.top(), half, r.height()),
                             QStringLiteral("I"), on ? onAccent : colors.textSecondary);
            break;
        }
        case ControlSwitchLook::Checkbox: {
            painter.setPen(QPen(colors.text, style.lineWidth));
            painter.setBrush(withAlpha(colors.text, t));
            painter.drawRect(r);
            if (t > 0.0) {
                QPen check(withAlpha(colors.background, t), 2.0);
                check.setCapStyle(Qt::RoundCap);
                check.setJoinStyle(Qt::RoundJoin);
                painter.setPen(check);
                painter.setBrush(Qt::NoBrush);
                const qreal w = r.width();
                painter.drawPolyline(QPolygonF({QPointF(r.left() + w * 0.22, r.top() + w * 0.52),
                                                QPointF(r.left() + w * 0.42, r.top() + w * 0.72),
                                                QPointF(r.left() + w * 0.78, r.top() + w * 0.3)}));
            }
            break;
        }
        case ControlSwitchLook::Lever: {
            const QRectF plate(r.left() + 1.0, r.top(), 40.0, r.height());
            painter.setPen(QPen(kPlateEdge, 1.0));
            painter.setBrush(mixControlColor(colors.surface, Qt::black, 0.35));
            painter.drawRoundedRect(plate, 4.0, 4.0);
            const QPointF nut = plate.center();
            painter.setPen(QPen(kMetalEdge, 1.0));
            painter.setBrush(kMetal);
            painter.drawEllipse(nut, 9.0, 9.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(kMetalDark);
            painter.drawEllipse(nut, 4.0, 4.0);
            const QPointF tip(nut.x(), nut.y() + (1.0 - 2.0 * t) * 14.0);
            QPen lever(kMetalLight, 5.0);
            lever.setCapStyle(Qt::RoundCap);
            painter.setPen(lever);
            painter.drawLine(nut, tip);
            painter.setPen(QPen(kMetalBallEdge, 1.0));
            painter.setBrush(kMetalBall);
            painter.drawEllipse(tip, 4.5, 4.5);
            paintLensLed(painter, QPointF(plate.right() + 13.0, nut.y()), 6.0, colors.success, on);
            break;
        }
        case ControlSwitchLook::Cells: {
            QFont cellFont = fixedPitch(font);
            painter.setFont(cellFont);
            const qreal gap = 4.0;
            const qreal cellWidth = (r.width() - gap) / 2.0;
            const QString texts[2] = {
                controlStyledText(style, offText),
                controlStyledText(style, onText),
            };
            for (int i = 0; i < 2; ++i) {
                const bool active = (i == 1) == on;
                const QRectF cell(r.left() + i * (cellWidth + gap), r.top(), cellWidth, r.height());
                if (active) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(colors.accent);
                } else {
                    painter.setPen(QPen(withAlpha(colors.accent, 0.4), style.lineWidth));
                    painter.setBrush(Qt::NoBrush);
                }
                painter.drawRect(cell);
                drawCenteredText(painter, cell, texts[i],
                                 active ? colors.background : withAlpha(colors.accent, 0.6));
            }
            break;
        }
    }
    painter.restore();
}

qreal controlSliderHeight(const ControlStyle& style) {
    switch (style.slider) {
        case ControlSliderLook::Ruler:
            return 40.0;
        case ControlSliderLook::Line:
            return 30.0;
        case ControlSliderLook::Fader:
            return 46.0;
        case ControlSliderLook::Track:
        case ControlSliderLook::Blocks:
            break;
    }
    return 24.0;
}

ControlSliderTrack controlSliderTrack(const QRectF& rect, const ControlStyle& style) {
    ControlSliderTrack track;
    qreal inset = 8.0;
    track.y = rect.center().y();
    switch (style.slider) {
        case ControlSliderLook::Track:
            break;
        case ControlSliderLook::Ruler:
            inset = 8.0;
            track.y = rect.center().y() - 6.0;
            break;
        case ControlSliderLook::Line:
            inset = 6.0;
            track.y = rect.center().y() + 5.0;
            break;
        case ControlSliderLook::Fader:
            inset = 8.0;
            track.y = rect.center().y() + 6.0;
            break;
        case ControlSliderLook::Blocks:
            inset = 0.0;
            break;
    }
    track.left = rect.left() + inset;
    track.right = qMax(track.left + 1.0, rect.right() - inset);
    return track;
}

qreal controlSliderFractionAt(const ControlSliderTrack& track, qreal x) {
    return qBound(0.0, (x - track.left) / (track.right - track.left), 1.0);
}

void paintControlSlider(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                        const ControlColors& colors, const QFont& font, qreal fraction, bool down,
                        bool enabled, const QStringList& scaleLabels) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!enabled) {
        painter.setOpacity(kDisabledOpacity);
    }
    fraction = qBound(0.0, fraction, 1.0);
    const ControlSliderTrack track = controlSliderTrack(rect, style);
    const qreal width = track.right - track.left;
    const qreal x = track.left + width * fraction;
    const qreal y = track.y;

    switch (style.slider) {
        case ControlSliderLook::Track: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.surfaceAlt);
            painter.drawRoundedRect(QRectF(track.left, y - 2.0, width, 4.0), 2.0, 2.0);
            painter.setBrush(colors.accent);
            painter.drawRoundedRect(QRectF(track.left, y - 2.0, x - track.left, 4.0), 2.0, 2.0);
            painter.setPen(QPen(colors.background, 3.0));
            painter.setBrush(down ? mixControlColor(colors.accent, Qt::white, 0.15)
                                  : colors.accent);
            painter.drawEllipse(QPointF(x, y), 8.0, 8.0);
            break;
        }
        case ControlSliderLook::Ruler: {
            painter.setPen(QPen(colors.textSecondary, 1.0));
            painter.drawLine(QPointF(track.left, y), QPointF(track.right, y));
            for (int i = 0; i <= 10; ++i) {
                const qreal tx = track.left + width * i / 10.0;
                painter.drawLine(QPointF(tx, y + 5.0), QPointF(tx, y + (i % 5 ? 9.0 : 12.0)));
            }
            if (scaleLabels.size() == 3) {
                QFont scaleFont = fixedPitch(font);
                scaleFont.setPixelSize(qMax(9, QFontInfo(font).pixelSize() - 2));
                painter.setFont(scaleFont);
                const qreal labelTop = y + 13.0;
                const QFontMetricsF metrics(scaleFont);
                const qreal xs[3] = {track.left, track.left + width / 2.0, track.right};
                for (int i = 0; i < 3; ++i) {
                    const qreal w = metrics.horizontalAdvance(scaleLabels[i]) + 2.0;
                    qreal left = xs[i] - w / 2.0;
                    left = qBound(rect.left(), left, rect.right() - w);
                    painter.drawText(QRectF(left, labelTop, w, metrics.height()), Qt::AlignCenter,
                                     scaleLabels[i]);
                }
            }
            const QRectF cap(x - 5.0, y - 10.0, 10.0, 20.0);
            painter.setPen(QPen(colors.text, 1.0));
            painter.setBrush(down ? mixControlColor(colors.surfaceAlt, colors.text, 0.12)
                                  : colors.surfaceAlt);
            painter.drawRoundedRect(cap, 1.0, 1.0);
            painter.setPen(QPen(colors.accent, 2.0));
            painter.drawLine(QPointF(x, y - 6.0), QPointF(x, y + 6.0));
            break;
        }
        case ControlSliderLook::Line: {
            painter.setPen(QPen(colors.text, style.lineWidth));
            painter.drawLine(QPointF(track.left, y), QPointF(track.right, y));
            painter.drawLine(QPointF(track.left, y - 4.0), QPointF(track.left, y + 4.0));
            painter.drawLine(QPointF(track.right, y - 4.0), QPointF(track.right, y + 4.0));
            painter.setPen(QPen(colors.accent, 1.5));
            painter.drawLine(QPointF(x, y - 9.0), QPointF(x, y + 9.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.accent);
            const qreal half = down ? 6.0 : 5.0;
            painter.drawPolygon(QPolygonF(
                {QPointF(x - half, y - 15.0), QPointF(x + half, y - 15.0), QPointF(x, y - 9.0)}));
            break;
        }
        case ControlSliderLook::Fader: {
            painter.setPen(QPen(withAlpha(colors.textSecondary, 0.7), 1.0));
            for (int i = 0; i <= 10; ++i) {
                const qreal tx = track.left + width * i / 10.0;
                painter.drawLine(QPointF(tx, y - 22.0), QPointF(tx, y - (i % 5 ? 18.0 : 15.0)));
            }
            painter.setPen(QPen(kSlotEdge, 1.0));
            painter.setBrush(kSlot);
            painter.drawRoundedRect(QRectF(track.left, y - 3.0, width, 6.0), 3.0, 3.0);
            const QRectF cap(x - 7.0, y - 12.0, 14.0, 24.0);
            painter.setPen(QPen(kMetalEdge, 1.0));
            painter.setBrush(down ? mixControlColor(kFaderCap, Qt::white, 0.2) : kFaderCap);
            painter.drawRoundedRect(cap, 2.0, 2.0);
            painter.setPen(QPen(kFaderGroove, 1.0));
            painter.drawLine(QPointF(x - 4.0, y - 8.0), QPointF(x - 4.0, y + 8.0));
            painter.drawLine(QPointF(x + 4.0, y - 8.0), QPointF(x + 4.0, y + 8.0));
            painter.setPen(QPen(Qt::white, 2.0));
            painter.drawLine(QPointF(x, y - 10.0), QPointF(x, y + 10.0));
            break;
        }
        case ControlSliderLook::Blocks: {
            constexpr qreal kCellGap = 2.0;
            const int count = qMax(6, int(qRound(width / 9.0)));
            const qreal cellWidth = (width - (count - 1) * kCellGap) / count;
            const int filled = qRound(fraction * count);
            for (int i = 0; i < count; ++i) {
                const QRectF cell(track.left + i * (cellWidth + kCellGap), y - 7.0, cellWidth,
                                  14.0);
                if (i < filled) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(down && i == filled - 1
                                         ? mixControlColor(colors.accent, Qt::white, 0.3)
                                         : colors.accent);
                    painter.drawRect(cell);
                } else {
                    painter.setPen(QPen(withAlpha(colors.accent, 0.35), 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
                }
            }
            break;
        }
    }
    painter.restore();
}

QSizeF controlReadoutSize(const ControlStyle& style, const QFont& font, const QString& widest) {
    const QFontMetricsF metrics(controlTextFont(style, font));
    const qreal textWidth = metrics.horizontalAdvance(widest);
    if (style.readout == ControlReadoutLook::Plain) {
        return {textWidth + 2.0, metrics.height()};
    }
    return {textWidth + 12.0, metrics.height() + 6.0};
}

void paintControlReadout(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                         const ControlColors& colors, const QFont& font, const QString& text,
                         bool enabled) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!enabled) {
        painter.setOpacity(kDisabledOpacity);
    }
    QFont valueFont = controlTextFont(style, font);
    const QColor ink = style.accentReadout ? colors.accent : colors.text;
    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    switch (style.readout) {
        case ControlReadoutLook::Plain:
            if (!style.monoText && !style.serifText) {
                valueFont.setWeight(QFont::Medium);
            }
            painter.setFont(valueFont);
            painter.setPen(ink);
            painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, text);
            break;
        case ControlReadoutLook::Boxed:
            painter.setPen(QPen(colors.frame, style.lineWidth));
            painter.setBrush(colors.background);
            painter.drawRoundedRect(r, style.cornerRadius, style.cornerRadius);
            painter.setFont(valueFont);
            painter.setPen(ink);
            painter.drawText(rect.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignRight | Qt::AlignVCenter,
                             text);
            break;
        case ControlReadoutLook::Display:
            painter.setPen(QPen(kSlotEdge, 1.0));
            painter.setBrush(kDisplay);
            painter.drawRoundedRect(r, style.cornerRadius, style.cornerRadius);
            painter.setFont(fixedPitch(valueFont));
            painter.setPen(withAlpha(ink, 0.12));
            painter.drawText(rect.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignRight | Qt::AlignVCenter,
                             unlitDigits(text));
            painter.setPen(ink);
            painter.drawText(rect.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignRight | Qt::AlignVCenter,
                             text);
            break;
    }
    painter.restore();
}

void paintControlLed(QPainter& painter, const QPointF& center, qreal radius,
                     const ControlStyle& style, const ControlColors& colors, const QColor& color,
                     bool on, bool enabled) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!enabled) {
        painter.setOpacity(kDisabledOpacity);
    }
    const qreal r = radius;
    switch (style.led) {
        case ControlLedLook::Dot: {
            painter.setPen(Qt::NoPen);
            if (on) {
                QRadialGradient halo(center, r * kControlLedReach);
                halo.setColorAt(0.6, withAlpha(color, 0.3));
                halo.setColorAt(1.0, withAlpha(color, 0.0));
                painter.setBrush(halo);
                painter.drawEllipse(center, r * kControlLedReach, r * kControlLedReach);
            }
            painter.setBrush(on ? color : mixControlColor(color, colors.surface, 0.75));
            painter.drawEllipse(center, r, r);
            break;
        }
        case ControlLedLook::Bezel: {
            const qreal ring = r * 1.45;
            painter.setPen(QPen(mixControlColor(colors.surface, colors.text, 0.5), 1.0));
            painter.setBrush(mixControlColor(colors.surface, colors.text, 0.2));
            painter.drawEllipse(center, ring, ring);
            painter.setPen(Qt::NoPen);
            painter.setBrush(on ? color : mixControlColor(color, Qt::black, 0.6));
            painter.drawEllipse(center, r, r);
            painter.setBrush(withAlpha(Qt::white, on ? 0.6 : 0.15));
            painter.drawEllipse(QPointF(center.x() - r * 0.3, center.y() - r * 0.3), r * 0.28,
                                r * 0.28);
            break;
        }
        case ControlLedLook::Ring:
            painter.setPen(QPen(colors.text, style.lineWidth));
            painter.setBrush(on ? color : QColor(Qt::transparent));
            painter.drawEllipse(center, r, r);
            break;
        case ControlLedLook::Lens:
            paintLensLed(painter, center, r, color, on);
            break;
        case ControlLedLook::Pixel: {
            painter.setRenderHint(QPainter::Antialiasing, false);
            if (on) {
                const qreal glow = r * kControlLedReach;
                painter.setPen(Qt::NoPen);
                painter.setBrush(withAlpha(color, 0.18));
                painter.drawRect(QRectF(center.x() - glow, center.y() - glow, 2 * glow, 2 * glow));
                painter.setBrush(color);
                painter.drawRect(QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r));
            } else {
                painter.setPen(QPen(withAlpha(color, 0.45), 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(
                    QRectF(center.x() - r + 0.5, center.y() - r + 0.5, 2 * r - 1.0, 2 * r - 1.0));
            }
            break;
        }
    }
    painter.restore();
}

}  // namespace traceview

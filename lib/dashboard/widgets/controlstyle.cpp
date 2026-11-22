#include "controlstyle.h"

#include <QCoreApplication>

namespace traceview {

QString controlStyleIdString(ControlStyleId id) {
    switch (id) {
        case ControlStyleId::Engineering:
            return QStringLiteral("engineering");
        case ControlStyleId::Scientific:
            return QStringLiteral("scientific");
        case ControlStyleId::Panel:
            return QStringLiteral("panel");
        case ControlStyleId::Retro:
            return QStringLiteral("retro");
        case ControlStyleId::Dashboard:
            break;
    }
    return QStringLiteral("dashboard");
}

ControlStyleId controlStyleFromId(const QString& id) {
    for (ControlStyleId style : allControlStyles()) {
        if (controlStyleIdString(style) == id) {
            return style;
        }
    }
    return ControlStyleId::Dashboard;
}

QString controlStyleDisplayName(ControlStyleId id) {
    switch (id) {
        case ControlStyleId::Engineering:
            return QCoreApplication::translate("ControlStyle", "Engineering");
        case ControlStyleId::Scientific:
            return QCoreApplication::translate("ControlStyle", "Scientific");
        case ControlStyleId::Panel:
            return QCoreApplication::translate("ControlStyle", "Panel");
        case ControlStyleId::Retro:
            return QCoreApplication::translate("ControlStyle", "Retro");
        case ControlStyleId::Dashboard:
            break;
    }
    return QCoreApplication::translate("ControlStyle", "Dashboard");
}

QVector<ControlStyleId> allControlStyles() {
    return {ControlStyleId::Dashboard, ControlStyleId::Engineering, ControlStyleId::Scientific,
            ControlStyleId::Panel, ControlStyleId::Retro};
}

ControlStyle controlStyleFor(ControlStyleId id) {
    ControlStyle style;
    style.id = id;
    switch (id) {
        case ControlStyleId::Dashboard:
            break;
        case ControlStyleId::Engineering:
            style.button = ControlButtonLook::Striped;
            style.switchLook = ControlSwitchLook::Rocker;
            style.slider = ControlSliderLook::Ruler;
            style.led = ControlLedLook::Bezel;
            style.readout = ControlReadoutLook::Boxed;
            style.cornerRadius = 2.0;
            style.monoText = true;
            break;
        case ControlStyleId::Scientific:
            style.button = ControlButtonLook::Outline;
            style.switchLook = ControlSwitchLook::Checkbox;
            style.slider = ControlSliderLook::Line;
            style.led = ControlLedLook::Ring;
            style.readout = ControlReadoutLook::Plain;
            style.cornerRadius = 0.0;
            style.serifText = true;
            style.serifItalicTitle = true;
            style.brightTitles = true;
            break;
        case ControlStyleId::Panel:
            style.button = ControlButtonLook::Arcade;
            style.switchLook = ControlSwitchLook::Lever;
            style.slider = ControlSliderLook::Fader;
            style.led = ControlLedLook::Lens;
            style.readout = ControlReadoutLook::Display;
            style.cornerRadius = 3.0;
            style.titleLetterSpacing = 6.0;
            style.accentReadout = true;
            style.screws = true;
            break;
        case ControlStyleId::Retro:
            style.button = ControlButtonLook::Bracket;
            style.switchLook = ControlSwitchLook::Cells;
            style.slider = ControlSliderLook::Blocks;
            style.led = ControlLedLook::Pixel;
            style.readout = ControlReadoutLook::Plain;
            style.cornerRadius = 6.0;
            style.chamfer = true;
            style.monoText = true;
            style.lowercaseText = true;
            style.titlePrefix = QStringLiteral("> ");
            style.accentTitles = true;
            style.accentReadout = true;
            break;
    }
    return style;
}

QColor mixControlColor(const QColor& a, const QColor& b, qreal t) {
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t),
                            float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QColor textOnColor(const QColor& fill) {
    // Relative luminance, sRGB weights; 0.55 rather than 0.5 keeps white
    // text on mid-tone accents (the default blues), as before.
    const qreal luminance = 0.2126 * fill.redF() + 0.7152 * fill.greenF() + 0.0722 * fill.blueF();
    return luminance > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

ControlColors controlColors(const ThemePalette& palette) {
    ControlColors colors;
    colors.background = palette.background;
    colors.surface = palette.surface;
    colors.surfaceAlt = palette.surfaceAlt;
    colors.border = palette.border;
    colors.frame = mixControlColor(palette.surface, palette.textPrimary, 0.45);
    colors.text = palette.textPrimary;
    colors.textSecondary = palette.textSecondary;
    colors.textDisabled = palette.textDisabled;
    colors.accent = palette.accent;
    colors.success = palette.success;
    colors.warning = palette.warning;
    colors.danger = palette.danger;
    return colors;
}

ControlTone controlToneFromId(const QString& id) {
    if (id == QLatin1String("success")) {
        return ControlTone::Success;
    }
    if (id == QLatin1String("warning")) {
        return ControlTone::Warning;
    }
    if (id == QLatin1String("danger")) {
        return ControlTone::Danger;
    }
    return ControlTone::Accent;
}

QColor controlToneColor(const ControlColors& colors, ControlTone tone) {
    switch (tone) {
        case ControlTone::Success:
            return colors.success;
        case ControlTone::Warning:
            return colors.warning;
        case ControlTone::Danger:
            return colors.danger;
        case ControlTone::Accent:
            break;
    }
    return colors.accent;
}

ControlStyleChoice parseControlStyleChoice(const QJsonObject& config) {
    ControlStyleChoice choice;
    const QString id = config.value(QLatin1String(kControlStyleKey)).toString();
    if (id.isEmpty() || id == QLatin1String(kControlStyleApp)) {
        return choice;
    }
    choice.followApp = false;
    choice.style = controlStyleFromId(id);
    return choice;
}

ControlStyleId effectiveControlStyle(const ControlStyleChoice& choice, const QString& appStyleId) {
    return choice.followApp ? controlStyleFromId(appStyleId) : choice.style;
}

QString controlStyledText(const ControlStyle& style, const QString& text) {
    return style.lowercaseText ? text.toLower() : text;
}

QString controlStyledTitle(const ControlStyle& style, const QString& title) {
    if (title.isEmpty()) {
        return title;
    }
    return style.titlePrefix + controlStyledText(style, title);
}

}  // namespace traceview

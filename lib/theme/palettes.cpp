#include "palettes.h"

#include <QCoreApplication>

namespace traceview {

ThemePalette makeDarkPalette() {
    ThemePalette p;
    p.id = "dark";
    // ThemePalette isn't a QObject, so tr() isn't available here; use
    // QCoreApplication::translate() with the owning class as context instead.
    p.displayName = QCoreApplication::translate("ThemeManager", "Dark");

    p.background = QColor("#0A0F1E");
    p.surface = QColor("#101A30");
    p.surfaceAlt = QColor("#16223D");

    p.border = QColor(255, 255, 255, 40);
    p.borderStrong = QColor("#FFFFFF");

    p.textPrimary = QColor("#F5F7FA");
    p.textSecondary = QColor("#96A2B8");
    p.textDisabled = QColor("#57607A");

    p.accent = QColor("#3D8BFF");
    p.accentHover = QColor("#5B9CFF");
    p.accentPressed = QColor("#2B6FE0");

    p.success = QColor("#35C48B");
    p.warning = QColor("#E8B339");
    p.danger = QColor("#E1544B");

    p.series = {p.accent, QColor("#FFFFFF"), p.warning, p.success, p.danger, QColor("#7DD3FC")};

    return p;
}

ThemePalette makeLightPalette() {
    ThemePalette p;
    p.id = "light";
    p.displayName = QCoreApplication::translate("ThemeManager", "Light");

    p.background = QColor("#FFFFFF");
    p.surface = QColor("#F4F6FA");
    p.surfaceAlt = QColor("#E7EBF3");

    // Alpha higher than the dark theme's equivalent (40 on a near-black
    // background) because the same low alpha reads much fainter against
    // white — low contrast in light mode was reported directly, see
    // docs/VISUAL_IDENTITY.md.
    p.border = QColor(10, 15, 30, 56);
    p.borderStrong = QColor("#0A0F1E");

    p.textPrimary = QColor("#0A0F1E");
    p.textSecondary = QColor("#4B5568");
    p.textDisabled = QColor("#9AA5B8");

    p.accent = QColor("#2E6FE0");
    p.accentHover = QColor("#4C86EE");
    p.accentPressed = QColor("#1F57BE");

    p.success = QColor("#1E9E6B");
    p.warning = QColor("#B9821F");
    p.danger = QColor("#C43D34");

    p.series = {p.accent, QColor("#0A0F1E"), p.warning, p.success, p.danger, QColor("#0891B2")};

    return p;
}

} // namespace traceview

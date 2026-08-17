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

ThemePalette makeWoodPalette() {
    ThemePalette p;
    p.id = "wood";
    p.displayName = QCoreApplication::translate("ThemeManager", "Wood");

    p.background = QColor("#2B1D14");
    p.surface = QColor("#3A2A1D");
    p.surfaceAlt = QColor("#4A3526");

    p.border = QColor(242, 228, 208, 40);
    p.borderStrong = QColor("#D9B98A");

    p.textPrimary = QColor("#F2E4D0");
    p.textSecondary = QColor("#C4A882");
    p.textDisabled = QColor("#7A6650");

    p.accent = QColor("#C97A3D");
    p.accentHover = QColor("#E0904F");
    p.accentPressed = QColor("#A65F2B");

    p.success = QColor("#7CA855");
    p.warning = QColor("#D9A441");
    p.danger = QColor("#C1523E");

    p.series = {p.accent, p.textPrimary, p.warning, p.success, p.danger, QColor("#8C6B4F")};

    return p;
}

ThemePalette makeBlackPalette() {
    ThemePalette p;
    p.id = "black";
    p.displayName = QCoreApplication::translate("ThemeManager", "Black");

    // Pure-black variant of Dark -- no navy undertone, plus a neutral
    // grayscale accent instead of blue for a starker, more monochrome look.
    p.background = QColor("#000000");
    p.surface = QColor("#0D0D0D");
    p.surfaceAlt = QColor("#1A1A1A");

    p.border = QColor(255, 255, 255, 30);
    p.borderStrong = QColor("#FFFFFF");

    p.textPrimary = QColor("#F5F5F5");
    p.textSecondary = QColor("#9E9E9E");
    p.textDisabled = QColor("#4D4D4D");

    p.accent = QColor("#D9D9D9");
    p.accentHover = QColor("#FFFFFF");
    p.accentPressed = QColor("#ABABAB");

    p.success = QColor("#4CAF50");
    p.warning = QColor("#FFB300");
    p.danger = QColor("#E53935");

    p.series = {p.accent, QColor("#FFFFFF"), p.warning, p.success, p.danger, QColor("#757575")};

    return p;
}

ThemePalette makeMatrixPalette() {
    ThemePalette p;
    p.id = "matrix";
    p.displayName = QCoreApplication::translate("ThemeManager", "Matrix");

    p.background = QColor("#000000");
    p.surface = QColor("#001A00");
    p.surfaceAlt = QColor("#003300");

    p.border = QColor(0, 255, 65, 40);
    p.borderStrong = QColor("#00FF41");

    p.textPrimary = QColor("#00FF41");
    p.textSecondary = QColor("#00B32C");
    p.textDisabled = QColor("#0D4D1A");

    p.accent = QColor("#00FF41");
    p.accentHover = QColor("#5CFF85");
    p.accentPressed = QColor("#00B32C");

    // Warning/danger stay off the green ramp -- on a monochrome-green theme,
    // status colors need to break the palette to still read as alerts.
    p.success = QColor("#00FF41");
    p.warning = QColor("#CFFF00");
    p.danger = QColor("#FF3131");

    p.series = {p.accent, QColor("#5CFF85"), p.warning, p.success, p.danger, QColor("#00B32C")};

    return p;
}

ThemePalette makeSynthwavePalette() {
    ThemePalette p;
    p.id = "synthwave";
    p.displayName = QCoreApplication::translate("ThemeManager", "Synthwave");

    p.background = QColor("#1A1025");
    p.surface = QColor("#241535");
    p.surfaceAlt = QColor("#2F1B45");

    p.border = QColor(255, 46, 154, 40);
    p.borderStrong = QColor("#FF2E9A");

    p.textPrimary = QColor("#F5E8FF");
    p.textSecondary = QColor("#B48AD1");
    p.textDisabled = QColor("#5C4570");

    p.accent = QColor("#FF2E9A");
    p.accentHover = QColor("#FF5CB3");
    p.accentPressed = QColor("#D4147A");

    p.success = QColor("#05FFA1");
    p.warning = QColor("#FFE156");
    p.danger = QColor("#FF3860");

    // Cyan alongside the pink accent is the other half of the classic
    // synthwave duo -- keeping both in series makes multi-series charts read
    // as unmistakably "synthwave" instead of just another pink theme.
    p.series = {p.accent, QColor("#00F5FF"), p.warning, p.success, p.danger, QColor("#B48AD1")};

    return p;
}

ThemePalette makeAmberPalette() {
    ThemePalette p;
    p.id = "amber";
    p.displayName = QCoreApplication::translate("ThemeManager", "Amber");

    // Same monochrome-on-black approach as Matrix, swapped to the other
    // classic CRT phosphor color (amber P3 tubes vs. green P1).
    p.background = QColor("#000000");
    p.surface = QColor("#1A0F00");
    p.surfaceAlt = QColor("#2E1B00");

    p.border = QColor(255, 176, 0, 40);
    p.borderStrong = QColor("#FFB000");

    p.textPrimary = QColor("#FFB000");
    p.textSecondary = QColor("#CC8800");
    p.textDisabled = QColor("#664400");

    p.accent = QColor("#FFB000");
    p.accentHover = QColor("#FFC94D");
    p.accentPressed = QColor("#CC8800");

    p.success = QColor("#FFB000");
    p.warning = QColor("#FFD966");
    p.danger = QColor("#FF4433");

    p.series = {p.accent, QColor("#FFC94D"), p.warning, p.success, p.danger, QColor("#CC8800")};

    return p;
}

ThemePalette makeArcticPalette() {
    ThemePalette p;
    p.id = "arctic";
    p.displayName = QCoreApplication::translate("ThemeManager", "Arctic");

    p.background = QColor("#2E3440");
    p.surface = QColor("#3B4252");
    p.surfaceAlt = QColor("#434C5E");

    p.border = QColor(236, 239, 244, 30);
    p.borderStrong = QColor("#ECEFF4");

    p.textPrimary = QColor("#ECEFF4");
    p.textSecondary = QColor("#D8DEE9");
    p.textDisabled = QColor("#4C566A");

    p.accent = QColor("#88C0D0");
    p.accentHover = QColor("#8FBCBB");
    p.accentPressed = QColor("#5E81AC");

    p.success = QColor("#A3BE8C");
    p.warning = QColor("#EBCB8B");
    p.danger = QColor("#BF616A");

    p.series = {p.accent, QColor("#B48EAD"), p.warning, p.success, p.danger, QColor("#5E81AC")};

    return p;
}

ThemePalette makeSakuraPalette() {
    ThemePalette p;
    p.id = "sakura";
    p.displayName = QCoreApplication::translate("ThemeManager", "Sakura");

    p.background = QColor("#FFF5F7");
    p.surface = QColor("#FDE8ED");
    p.surfaceAlt = QColor("#FBD9E3");

    p.border = QColor(219, 39, 119, 40);
    p.borderStrong = QColor("#D6336C");

    // Deep maroon rather than black -- keeps the whole palette in the same
    // warm-pink family instead of a jarring neutral-gray text color.
    p.textPrimary = QColor("#4A1D2C");
    p.textSecondary = QColor("#8C5468");
    p.textDisabled = QColor("#C9A0AF");

    p.accent = QColor("#E85D8A");
    p.accentHover = QColor("#F17BA3");
    p.accentPressed = QColor("#C43D6C");

    p.success = QColor("#6FAE7D");
    p.warning = QColor("#D9A441");
    p.danger = QColor("#C43D34");

    p.series = {p.accent, QColor("#8E6C9E"), p.warning, p.success, p.danger, QColor("#5E9FBF")};

    return p;
}

} // namespace traceview

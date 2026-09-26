#pragma once

#include <QObject>
#include <QVector>

#include "traceview/appearance.h"
#include "traceview/framestyle.h"
#include "traceview/theme.h"

namespace traceview {

// Owns the set of available ThemePalettes, tracks which one is active, and
// applies it to the running QApplication as a stylesheet. Also owns the
// active frame style (traceview/framestyle.h): the palette decides colors,
// the frame decides container and control shapes, and both feed the same
// stylesheet. Persists both selections across launches via QSettings.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    const ThemePalette& currentTheme() const;
    QVector<ThemePalette> availableThemes() const;

    // Adds a new selectable template. No-op if `palette.id` is already registered.
    void registerTheme(const ThemePalette& palette);

    void setTheme(const QString& id);

    const FrameStyle& currentFrameStyle() const;
    // Persists the choice, rebuilds the stylesheet (control radii) and emits
    // themeChanged() so every custom-painted container reshapes, then
    // frameStyleChanged() for the menus that show the choice.
    void setFrameStyle(const QString& id);

    // The other appearance choices (traceview/appearance.h). Every setter
    // persists its choice, re-applies the theme (themeChanged(), so
    // everything repaints/relays out) and emits appearanceChanged(); an
    // unknown id falls back to that option's default.
    DataColorsId dataColors() const {
        return m_dataColors;
    }
    void setDataColors(const QString& id);
    // The color series `index` is drawn in -- `own` (its configured color)
    // unless the data color scheme overrides it.
    QColor seriesColor(int index, const QColor& own) const;

    const Density& currentDensity() const;
    void setDensity(const QString& id);

    // Hover falls back to Line on touch-only platforms (no hover there);
    // cardHeaderChoice() is the stored choice, for pickers.
    CardHeaderId cardHeader() const;
    CardHeaderId cardHeaderChoice() const {
        return m_cardHeader;
    }
    void setCardHeader(const QString& id);

    CanvasId canvas() const {
        return m_canvas;
    }
    void setCanvas(const QString& id);

    MotionId motion() const {
        return m_motion;
    }
    bool reduceMotion() const {
        return m_motion == MotionId::Reduced;
    }
    void setMotion(const QString& id);

    // User-made palettes: added or replaced by id (ids start with
    // "custom:", see isCustomPaletteId()), persisted, and listed by
    // availableThemes() after the built-in ones. Removing the active one
    // switches back to the default palette. Both emit themesChanged().
    void saveCustomTheme(const ThemePalette& palette);
    void removeCustomTheme(const QString& id);

    // Re-applies the current theme's stylesheet to QApplication. Call once at
    // startup, after QApplication exists and before the first window shows.
    void applyCurrentTheme();

signals:
    void themeChanged(const ThemePalette& palette);
    void frameStyleChanged();
    // Any appearance choice changed -- palette, frame, or one of the
    // options above. For menus and pickers that show the current choices.
    void appearanceChanged();
    // The list of palettes changed (a custom one was saved or removed).
    void themesChanged();

private:
    ThemeManager();

    int indexOf(const QString& id) const;

    QVector<ThemePalette> m_themes;
    int m_currentIndex = 0;
    FrameStyleId m_frameStyle = FrameStyleId::Rounded;
    DataColorsId m_dataColors = DataColorsId::Custom;
    DensityId m_density = DensityId::Normal;
    CardHeaderId m_cardHeader = CardHeaderId::Filled;
    CanvasId m_canvas = CanvasId::Plain;
    MotionId m_motion = MotionId::Animated;

    void persistCustomThemes() const;
    // Persists `value` under appearance/`key`, re-applies the theme and
    // emits appearanceChanged().
    void commitAppearance(const char* key, const QString& value);
};

}  // namespace traceview

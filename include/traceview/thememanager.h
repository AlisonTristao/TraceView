#pragma once

#include <QObject>
#include <QVector>

#include "traceview/theme.h"

namespace traceview {

// Owns the set of available ThemePalettes, tracks which one is active, and
// applies it to the running QApplication as a stylesheet. Persists the
// selection across launches via QSettings.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    const ThemePalette& currentTheme() const;
    QVector<ThemePalette> availableThemes() const;

    // Adds a new selectable template. No-op if `palette.id` is already registered.
    void registerTheme(const ThemePalette& palette);

    void setTheme(const QString& id);

    // Re-applies the current theme's stylesheet to QApplication. Call once at
    // startup, after QApplication exists and before the first window shows.
    void applyCurrentTheme();

signals:
    void themeChanged(const ThemePalette& palette);

private:
    ThemeManager();

    int indexOf(const QString& id) const;

    QVector<ThemePalette> m_themes;
    int m_currentIndex = 0;
};

}  // namespace traceview

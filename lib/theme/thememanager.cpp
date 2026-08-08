#include "traceview/thememanager.h"

#include <QApplication>
#include <QSettings>

#include "palettes.h"
#include "stylesheet.h"

namespace traceview {

namespace {
constexpr const char* kSettingsKey = "appearance/theme";
constexpr const char* kDefaultThemeId = "dark";
} // namespace

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager() {
    registerTheme(makeDarkPalette());
    registerTheme(makeLightPalette());

    const QSettings settings;
    const QString savedId = settings.value(kSettingsKey, kDefaultThemeId).toString();
    const int idx = indexOf(savedId);
    m_currentIndex = idx >= 0 ? idx : 0;
}

const ThemePalette& ThemeManager::currentTheme() const {
    return m_themes[m_currentIndex];
}

QVector<ThemePalette> ThemeManager::availableThemes() const {
    return m_themes;
}

void ThemeManager::registerTheme(const ThemePalette& palette) {
    if (indexOf(palette.id) >= 0) {
        return;
    }
    m_themes.append(palette);
}

void ThemeManager::setTheme(const QString& id) {
    const int idx = indexOf(id);
    if (idx < 0) {
        return;
    }
    m_currentIndex = idx;

    QSettings settings;
    settings.setValue(kSettingsKey, id);

    applyCurrentTheme();
}

void ThemeManager::applyCurrentTheme() {
    if (auto* app = qApp) {
        app->setStyleSheet(buildStyleSheet(currentTheme()));
    }
    emit themeChanged(currentTheme());
}

int ThemeManager::indexOf(const QString& id) const {
    for (int i = 0; i < m_themes.size(); ++i) {
        if (m_themes[i].id == id) {
            return i;
        }
    }
    return -1;
}

} // namespace traceview

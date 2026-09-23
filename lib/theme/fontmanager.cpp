#include "traceview/fontmanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>

namespace traceview {

namespace {
constexpr const char* kSettingsKey = "appearance/font";
constexpr const char* kDefaultFontId = "system";
}  // namespace

FontManager& FontManager::instance() {
    static FontManager manager;
    return manager;
}

FontManager::FontManager() {
    if (auto* app = qApp) {
        m_baseFont = app->font();
    }

    registerFont({"system", QCoreApplication::translate("FontManager", "System Default"), ""});
    // Fallbacks: Android system fonts first, then common Linux ones.
    registerFont({"consolas",
                  "Consolas",
                  "Consolas",
                  {"Droid Sans Mono", "Cutive Mono", "DejaVu Sans Mono", "Liberation Mono",
                   "monospace"},
                  QFont::Monospace});
    registerFont({"georgia",
                  "Georgia",
                  "Georgia",
                  {"Noto Serif", "Droid Serif", "DejaVu Serif", "Liberation Serif", "serif"},
                  QFont::Serif});
    registerFont({"verdana",
                  "Verdana",
                  "Verdana",
                  {"DejaVu Sans", "Noto Sans", "Droid Sans", "sans-serif"},
                  QFont::SansSerif});

    const QSettings settings;
    const QString savedId = settings.value(kSettingsKey, kDefaultFontId).toString();
    const int idx = indexOf(savedId);
    m_currentIndex = idx >= 0 ? idx : 0;
}

const FontOption& FontManager::currentFont() const {
    return m_fonts[m_currentIndex];
}

QVector<FontOption> FontManager::availableFonts() const {
    return m_fonts;
}

void FontManager::registerFont(const FontOption& font) {
    if (indexOf(font.id) >= 0) {
        return;
    }
    m_fonts.append(font);
}

void FontManager::setFont(const QString& id) {
    const int idx = indexOf(id);
    if (idx < 0) {
        return;
    }
    m_currentIndex = idx;

    QSettings settings;
    settings.setValue(kSettingsKey, id);

    applyCurrentFont();
}

void FontManager::applyCurrentFont() {
    if (auto* app = qApp) {
        app->setFont(fontForOption(m_baseFont, currentFont()));

        // QApplication::setFont() alone doesn't repolish widgets that were
        // already styled by the app-wide QSS (see ThemeManager) -- their
        // font only catches up on the next style recalculation, which is
        // why menus otherwise stayed on the old font until restart.
        // Re-setting the same style sheet forces that recalculation now.
        app->setStyleSheet(app->styleSheet());
    }
    emit fontChanged(currentFont());
}

int FontManager::indexOf(const QString& id) const {
    for (int i = 0; i < m_fonts.size(); ++i) {
        if (m_fonts[i].id == id) {
            return i;
        }
    }
    return -1;
}

QFont fontForOption(QFont base, const FontOption& option) {
    if (option.family.isEmpty()) {
        return base;
    }
    // setFamilies (not setFamily): the family list is what lets the
    // fallbacks win over Qt's generic substitution when `family` is missing.
    base.setFamilies(QStringList{option.family} + option.fallbackFamilies);
    base.setStyleHint(option.styleHint);
    return base;
}

QFont scaledFont(QFont font, qreal factor) {
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * factor));
    }
    return font;
}

}  // namespace traceview

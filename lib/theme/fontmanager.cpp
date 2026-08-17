#include "traceview/fontmanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>

namespace traceview {

namespace {
constexpr const char* kSettingsKey = "appearance/font";
constexpr const char* kDefaultFontId = "system";
} // namespace

FontManager& FontManager::instance() {
    static FontManager manager;
    return manager;
}

FontManager::FontManager() {
    if (auto* app = qApp) {
        m_baseFont = app->font();
    }

    registerFont({"system", QCoreApplication::translate("FontManager", "System Default"), ""});
    registerFont({"consolas", "Consolas", "Consolas"});
    registerFont({"georgia", "Georgia", "Georgia"});
    registerFont({"verdana", "Verdana", "Verdana"});

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
        const FontOption& font = currentFont();
        QFont f = m_baseFont;
        if (!font.family.isEmpty()) {
            f.setFamily(font.family);
        }
        app->setFont(f);
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

} // namespace traceview

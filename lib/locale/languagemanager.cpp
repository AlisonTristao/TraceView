#include "traceview/languagemanager.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace traceview {

namespace {
constexpr const char* kSettingsKey = "appearance/language";
constexpr const char* kDefaultLanguageId = "en";
} // namespace

LanguageManager& LanguageManager::instance() {
    static LanguageManager manager;
    return manager;
}

LanguageManager::LanguageManager() {
    registerLanguage({"en", "English", ""});
    registerLanguage({"pt_BR", "Português (Brasil)", ":/translations/traceview_pt_BR.qm"});

    const QSettings settings;
    int idx = indexOf(settings.value(kSettingsKey).toString());
    if (idx < 0) {
        // No saved preference yet -- try the system locale, first by full
        // name (e.g. "pt_BR") then by language only (e.g. "pt"), before
        // falling back to the default.
        const QString systemName = QLocale::system().name();
        idx = indexOf(systemName);
        if (idx < 0) {
            idx = indexOf(systemName.section('_', 0, 0));
        }
    }
    m_currentIndex = idx >= 0 ? idx : indexOf(kDefaultLanguageId);
    if (m_currentIndex < 0) {
        m_currentIndex = 0;
    }
}

const LanguageInfo& LanguageManager::currentLanguage() const {
    return m_languages[m_currentIndex];
}

QVector<LanguageInfo> LanguageManager::availableLanguages() const {
    return m_languages;
}

void LanguageManager::registerLanguage(const LanguageInfo& language) {
    if (indexOf(language.id) >= 0) {
        return;
    }
    m_languages.append(language);
}

void LanguageManager::setLanguage(const QString& id) {
    const int idx = indexOf(id);
    if (idx < 0) {
        return;
    }
    m_currentIndex = idx;

    QSettings settings;
    settings.setValue(kSettingsKey, id);

    applyCurrentLanguage();
}

void LanguageManager::applyCurrentLanguage() {
    if (auto* app = qApp) {
        app->removeTranslator(&m_appTranslator);
        app->removeTranslator(&m_qtTranslator);

        const LanguageInfo& language = currentLanguage();
        if (!language.qmResourcePath.isEmpty() && m_appTranslator.load(language.qmResourcePath)) {
            app->installTranslator(&m_appTranslator);
        }

        // Also load Qt's own prebuilt base translation for the same locale,
        // so native dialog chrome (e.g. default QMessageBox/QColorDialog
        // button text) follows the app's language too. Qt 6.7+ ships these
        // consolidated as "qt_<locale>.qm" (merging what used to be separate
        // qtbase_/qtdeclarative_/... catalogs) -- confirmed against this
        // Qt 6.9.2 install's deployed translations/ directory. Missing on
        // some Qt installs is fine -- load() just returns false silently, no
        // need to warn about it.
        const QString qtTranslationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (m_qtTranslator.load(QString("qt_%1").arg(language.id), qtTranslationsPath)) {
            app->installTranslator(&m_qtTranslator);
        }
    }
    emit languageChanged(currentLanguage());
}

int LanguageManager::indexOf(const QString& id) const {
    for (int i = 0; i < m_languages.size(); ++i) {
        if (m_languages[i].id == id) {
            return i;
        }
    }
    return -1;
}

} // namespace traceview

#pragma once

#include <QObject>
#include <QTranslator>
#include <QVector>

#include "traceview/locale.h"

namespace traceview {

// Owns the set of available LanguageInfos, tracks which one is active, and
// installs the matching QTranslators on the running QApplication. Persists
// the selection across launches via QSettings.
class LanguageManager : public QObject {
    Q_OBJECT

public:
    static LanguageManager& instance();

    const LanguageInfo& currentLanguage() const;
    QVector<LanguageInfo> availableLanguages() const;

    // Adds a new selectable language. No-op if `language.id` is already registered.
    void registerLanguage(const LanguageInfo& language);

    void setLanguage(const QString& id);

    // Re-installs the current language's translators (app strings, plus
    // Qt's own base translation) on QApplication. Call once at startup,
    // after QApplication exists and before the first window shows.
    void applyCurrentLanguage();

signals:
    void languageChanged(const LanguageInfo& language);

private:
    LanguageManager();

    int indexOf(const QString& id) const;

    QVector<LanguageInfo> m_languages;
    int m_currentIndex = 0;

    QTranslator m_appTranslator;
    QTranslator m_qtTranslator;
};

} // namespace traceview

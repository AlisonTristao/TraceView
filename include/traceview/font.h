#pragma once

#include <QFont>
#include <QString>
#include <QStringList>

namespace traceview {

// A selectable UI font. A "font" in the picker is just one of these -- adding
// a new one requires no other code changes, see docs/THEMING.md.
struct FontOption {
    QString id;           // stable key, e.g. "consolas"
    QString displayName;  // shown in the font picker, e.g. "Consolas"
    QString family;       // QFont family passed to QApplication::setFont();
                          // empty means "leave the platform default alone"
    // Tried in order when `family` isn't installed. Consolas/Georgia/Verdana
    // are Windows fonts that Android (and most Linux) lack, and without these
    // Qt substitutes the platform sans-serif -- every option looked the same
    // on mobile. List per-platform equivalents here.
    QStringList fallbackFamilies = {};
    // Last-resort generic class, used once no listed family matches.
    QFont::StyleHint styleHint = QFont::AnyStyle;
};

}  // namespace traceview

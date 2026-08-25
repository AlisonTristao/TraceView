#pragma once

#include <QString>

namespace traceview {

// A selectable UI font. A "font" in the picker is just one of these -- adding
// a new one requires no other code changes, see docs/THEMING.md.
struct FontOption {
    QString id;           // stable key, e.g. "consolas"
    QString displayName;  // shown in the font picker, e.g. "Consolas"
    QString family;       // QFont family passed to QApplication::setFont();
                          // empty means "leave the platform default alone"
};

}  // namespace traceview

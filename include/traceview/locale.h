#pragma once

#include <QString>

namespace traceview {

// A selectable UI language. A "language" in the picker is just one of these --
// adding a new one requires no other code changes, see docs/LOCALIZATION.md.
struct LanguageInfo {
    QString id;              // stable key, e.g. "pt_BR" (Qt locale name convention)
    QString displayName;     // shown in the language picker, in the language's own name (not
                             // translated), e.g. "Português (Brasil)"
    QString qmResourcePath;  // e.g. ":/translations/traceview_pt_BR.qm" -- empty for English (the
                             // source language, no .qm needed)
};

}  // namespace traceview

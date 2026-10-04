#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "lab/code/codemodule.h"

namespace tracelab::code {

// A block's code a benchmark ships (a Qt resource, read-only): what you open
// to start from, and what "back to the original" restores.
struct CodeTemplate {
    BlockKind kind = BlockKind::Controller;
    QString id;           // "linefollower/pd": stable, written into copies
    QString name;         // translated, for the picker
    QString description;  // translated, one line
    QString resourcePath;
};

// The templates and the user's own files. A file made from a template starts
// with the line
//     // tracelab: template <id>
// which is how its origin is known later ("modified", "back to the
// original"). The user's files live under Documents/TraceLab/<kind>s/ by
// default.
class CodeLibrary {
public:
    static CodeLibrary& instance();

    // Ignored when the id is taken.
    void registerTemplate(const CodeTemplate& codeTemplate);
    QVector<CodeTemplate> templates(BlockKind kind) const;
    const CodeTemplate* findTemplate(const QString& id) const;
    // The template's code, without the origin line.
    QString templateSource(const QString& id) const;

    // The empty block of each kind ("New").
    static QString skeletonSource(BlockKind kind);

    // Documents/TraceLab, created on demand (with QStandardPaths' test mode
    // on, a folder under the test's local data instead).
    static QString userRoot();
    // Documents/TraceLab/controllers (filters, plants), created on demand.
    static QString userDirectory(BlockKind kind);
    // What tells another editor where a block's headers are, in userRoot():
    // compile_flags.txt (clangd) and .vscode/c_cpp_properties.json (VS
    // Code's C/C++ extension, with the folder open), both pointing at the
    // runtime folder (CodeRuntime). Rewritten when missing or out of date;
    // a file without TraceLab's marker is the user's and stays. False, with
    // `error`, when one cannot be written.
    static bool writeEditorConfig(QString* error = nullptr);
    // The block files in userDirectory(kind), newest first.
    static QStringList userFiles(BlockKind kind);

    // A new file in userDirectory(kind) holding the template's code (with
    // the origin line) or, for an empty id, the skeleton. Named after the
    // template (or `baseName`), with a number when taken. Its path, or empty
    // with `error`.
    QString createFile(BlockKind kind, const QString& templateId, const QString& baseName,
                       QString* error) const;

    // The template id in a source's origin line, empty without one.
    static QString originOf(const QString& source);
    // The source with the origin line set to `templateId` (removed for an
    // empty id).
    static QString withOrigin(const QString& source, const QString& templateId);
    // True when the source differs from its template (origin line and line
    // endings aside); false without a template.
    bool isModified(const QString& source) const;
    // The template's code with its origin line: what "back to the original"
    // writes. Empty when the source has no (known) template.
    QString originalOf(const QString& source) const;

    static QString readFile(const QString& path, QString* error = nullptr);
    static bool writeFile(const QString& path, const QString& text, QString* error = nullptr);

    static QString kindFolder(BlockKind kind);

private:
    CodeLibrary() = default;
    QVector<CodeTemplate> m_templates;
};

}  // namespace tracelab::code

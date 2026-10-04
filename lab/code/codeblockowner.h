#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "lab/code/codemodule.h"

namespace tracelab {

// Where a code block's source comes from: a file of the user's, or a
// template a benchmark ships (read-only; editing it makes a file first).
// Plus the parameter values set by hand, by name.
struct CodeBlockSpec {
    QString path;
    QString templateId;
    QJsonObject params;

    bool isEmpty() const {
        return path.isEmpty() && templateId.isEmpty();
    }
    static CodeBlockSpec fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

// One line of the "signals" list next to a block's code: a heading, or a
// signal with its live value and the declaration a double-click inserts.
struct SignalRow {
    QString text;
    QString declaration;
    bool header = false;
};

// Whatever runs code blocks -- a controller device (its filters and its
// controller), a plant device (its plant) -- as the TraceLab tab's block
// editor sees it. Blocks are numbered by the owner. Every block's file is
// the owner's to watch and compile; a version that does not compile leaves
// the previous one running.
class CodeBlockOwner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual code::BlockKind blockKind(int block) const = 0;
    // "Controller", "Filter 1 of 2", "Plant"... (translated).
    virtual QString blockRole(int block) const = 0;
    // The block's display name (its file's base name or its template's).
    virtual QString blockName(int block) const = 0;
    virtual bool canRemove(int block) const {
        Q_UNUSED(block);
        return false;
    }

    virtual CodeBlockSpec spec(int block) const = 0;
    // Replaces the block's source and loads it.
    virtual void setSpec(int block, const CodeBlockSpec& spec) = 0;
    // Compiles the block again from its source (after its file changed).
    virtual void reload(int block) = 0;
    // The source as compiled (the file's or the template's).
    virtual QString source(int block) const = 0;

    // The running code (null when there is none).
    virtual const code::CodeModule* module(int block) const = 0;
    // The messages of the last compile, which may be of a version that did
    // not replace the running one.
    virtual QVector<code::CodeMessage> messages(int block) const = 0;
    virtual bool compiled(int block) const = 0;
    // A fault of the running code, "file:line: text", or empty.
    virtual QString runtimeError(int block) const = 0;
    // What printf()/tl_log() wrote since the last call.
    virtual QStringList takeLog(int block) = 0;
    // A parameter, live; kept in the owner's configuration.
    virtual void setParam(int block, const QString& name, double value) = 0;

    // How the block sits in the loop right now, for the side panel: a tone
    // (0 none, 1 good, 2 waiting, 3 wrong) and a sentence.
    struct Status {
        int tone = 0;
        QString text;
    };
    virtual Status status(int block) const = 0;
    // A remark under the status ("uses the preview..."), or empty.
    virtual QString note(int block) const {
        Q_UNUSED(block);
        return QString();
    }
    // What the block can read and write, with live values.
    virtual QVector<SignalRow> signalRows(int block) const = 0;

signals:
    // The owner's configuration changed: the project should store it.
    void configChanged();
    // A block was (re)loaded or its messages changed.
    void blocksChanged();
    // The link's state changed.
    void statusChanged();
};

// The rows for a list of named signals ("s[0]".."s[14]" grouped as s[15])
// with their values, under `title`. `declaration` is "TL_IN(%1);"-like: %1
// the name, %2 the array size (for an array the macro gets "_ARRAY").
void appendSignalRows(QVector<SignalRow>* rows, const QString& title, const QStringList& names,
                      const QVector<double>& values, const QString& scalarDeclaration,
                      const QString& arrayDeclaration, bool showValues = true);

}  // namespace tracelab

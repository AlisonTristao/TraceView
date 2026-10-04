#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "lab/code/codehostapi.h"

struct TCCState;

namespace tracelab::code {

// What a block is: which functions it defines and which declarations make
// sense in it (tracelab.h).
enum class BlockKind { Plant, Filter, Controller };

// One TL_* declaration of a compiled block, pointing at its variable in the
// running code.
struct Declaration {
    DeclKind kind = DeclKind::Param;
    QString name;
    QString unit;
    QStringList options;  // ParamEnum
    double defaultValue = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    int count = 1;
    // The variable: a double (array) for every kind but ParamInt/ParamEnum/
    // ParamBool, which are an int.
    void* variable = nullptr;
    // A state's der()/next() (double); a preview's y array.
    void* second = nullptr;

    bool isParam() const {
        return kind == DeclKind::Param || kind == DeclKind::ParamInt ||
               kind == DeclKind::ParamEnum || kind == DeclKind::ParamBool;
    }
    bool isIntParam() const {
        return kind == DeclKind::ParamInt || kind == DeclKind::ParamEnum ||
               kind == DeclKind::ParamBool;
    }
    double* values() const {
        return static_cast<double*>(variable);
    }
};

// A function (or variable) of the host the compiled code may call, beyond
// tracelab.h's: an environment's (the line follower's track...).
struct HostSymbol {
    QByteArray name;
    const void* address = nullptr;
};

// A compiler message (or the runtime fault) at a line of the block's file;
// line 0 when it has none.
struct CodeMessage {
    int line = 0;
    QString text;
    bool warning = false;
};

// One block of user C code compiled in memory (libtcc), with the variables of
// its declarations and its functions. Not copyable; compile() again to
// replace the code (the old one is freed).
class CodeModule {
public:
    CodeModule();
    ~CodeModule();
    CodeModule(const CodeModule&) = delete;
    CodeModule& operator=(const CodeModule&) = delete;

    // Compiles `source`, shown as `fileName` in messages. On failure the
    // module holds no code and messages() says why (warnings come along
    // either way). Parameter values set before are re-applied by name.
    bool compile(const QString& source, const QString& fileName,
                 const QVector<HostSymbol>& extraSymbols = {});
    bool isCompiled() const {
        return m_state != nullptr;
    }
    const QString& fileName() const {
        return m_fileName;
    }
    const QVector<CodeMessage>& messages() const {
        return m_messages;
    }
    // The first error, "file:line: text", or empty.
    QString errorText() const;

    // In declaration (source) order.
    const QVector<Declaration>& declarations() const {
        return m_declarations;
    }
    QVector<const Declaration*> declarations(DeclKind kind) const;
    const Declaration* find(DeclKind kind, const QString& name) const;
    bool hasDeclaration(DeclKind kind) const;

    bool hasFunction(const char* name) const;
    // Calls `void name(void)` with this block's context current and a fault
    // guard: false when the function is missing or it faulted (then
    // runtimeError() says where, and the block should be stopped until it
    // is reset or recompiled).
    bool call(const char* name);
    bool hasRuntimeError() const {
        return !m_runtimeError.text.isEmpty();
    }
    const CodeMessage& runtimeError() const {
        return m_runtimeError;
    }
    void clearRuntimeError();

    CodeContext& context() {
        return m_context;
    }
    const CodeContext& context() const {
        return m_context;
    }

    // Parameters by name: the value in the code until set. Set values are kept
    // across recompiles (by name, clamped to the new range).
    QVector<const Declaration*> params() const;
    double paramValue(const QString& name) const;
    bool setParamValue(const QString& name, double value);
    // The values set by hand (not the code's defaults), for the project.
    QJsonObject paramOverrides() const;
    void setParamOverrides(const QJsonObject& values);

    // Lines printf()/tl_log() wrote since the last call.
    QStringList takeLog();

private:
    void release();
    void applyOverride(const Declaration& decl, double value);
    static void onCompilerMessage(void* opaque, const char* message);
    static int onBacktrace(void* opaque, void* pc, const char* file, int line, const char* function,
                           const char* message);

    TCCState* m_state = nullptr;
    QString m_fileName;
    QVector<CodeMessage> m_messages;
    QVector<Declaration> m_declarations;
    QHash<QByteArray, void (*)()> m_functions;
    CodeMessage m_runtimeError;
    CodeContext m_context;
    QJsonObject m_overrides;
};

}  // namespace tracelab::code

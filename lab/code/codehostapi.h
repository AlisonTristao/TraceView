#pragma once

// The host side of tracelab.h: the layout of a declaration as the compiled
// code lays it out, and the functions the code calls (tl_ts(), tl_noise(),
// printf()...), which read the context of the block running right now.

#include <QStringList>
#include <QVector>
#include <QtGlobal>

struct TCCState;

namespace tracelab::code {

// tracelab.h's tl_decl, field for field.
struct TlDecl {
    int kind;
    int count;
    const char* name;
    const char* unit;
    const char* options;
    double def;
    double lo;
    double hi;
    void* ptr;
    void* ptr2;
};

// tracelab.h's TL_K_* values.
enum class DeclKind : int {
    Param = 1,
    ParamInt = 2,
    ParamEnum = 3,
    ParamBool = 4,
    Input = 10,
    State = 11,
    Output = 12,
    Discrete = 13,
    In = 20,
    Out = 21,
    InOut = 22,
    Preview = 23,
    Diag = 30,
};

// What the host functions read and write while one block runs. Each
// CodeModule owns one; CodeModule::call() makes it current.
struct CodeContext {
    double ts = 1.0 / 80.0;
    quint64 tick = 0;
    double time = 0.0;
    int previewCount = 0;

    // tl_noise()/tl_rand(): splitmix64 seeded with `seed`, restarted by
    // restartNoise() (on every reset), so a run is reproducible.
    quint64 seed = 1;
    quint64 rngState = 1;
    bool hasSpareGaussian = false;
    double spareGaussian = 0.0;
    void restartNoise();
    double uniform();
    double gaussian();

    // printf()/tl_log() output, whole lines; the newest kMaxLogLines kept.
    static constexpr int kMaxLogLines = 500;
    QStringList log;
    QString partialLine;
    void appendLog(const QString& text);

    // tl_disturb(i): what the simulation injects, by channel (sim.disturb).
    QVector<double> disturbances;
    // The environment the running block belongs to, for its own host
    // functions (PlantEnvironment::symbols()); null without one.
    void* environment = nullptr;

    // tl_set_iterations() of the last step.
    int iterations = 0;
    // tl_predicted() of the last step.
    QVector<double> predictedX;
    QVector<double> predictedY;
    QVector<double> predictedTheta;
};

// The context of the block running on this thread, null between calls.
CodeContext*& currentContext();

// Registers the C library subset and the tl_* functions of tracelab.h in
// `state`, before tcc_relocate().
void addHostSymbols(TCCState* state);

}  // namespace tracelab::code

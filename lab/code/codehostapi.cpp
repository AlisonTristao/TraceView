#include "codehostapi.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <Eigen/Dense>

#include "lab/numeric/boxqp.h"
#include "libtcc.h"

namespace tracelab::code {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

CodeContext* context() {
    return currentContext();
}

QString vformat(const char* format, va_list args) {
    char buffer[1024];
    const int written = std::vsnprintf(buffer, sizeof buffer, format, args);
    if (written < 0) {
        return QString();
    }
    return QString::fromUtf8(buffer, qMin(written, int(sizeof buffer) - 1));
}

// ---- the tl_* functions (C linkage, called from the compiled code) ----

extern "C" {

double tl_ts() {
    CodeContext* c = context();
    return c ? c->ts : 0.0;
}

double tl_fps() {
    CodeContext* c = context();
    return c && c->ts > 0.0 ? 1.0 / c->ts : 0.0;
}

long long tl_tick() {
    CodeContext* c = context();
    return c ? (long long)c->tick : 0;
}

double tl_time() {
    CodeContext* c = context();
    return c ? c->time : 0.0;
}

int tl_preview_count() {
    CodeContext* c = context();
    return c ? c->previewCount : 0;
}

double tl_noise(double sigma) {
    CodeContext* c = context();
    return c ? sigma * c->gaussian() : 0.0;
}

double tl_rand() {
    CodeContext* c = context();
    return c ? c->uniform() : 0.0;
}

double tl_nan() {
    return std::numeric_limits<double>::quiet_NaN();
}

int isnan_tl(double value) {
    return std::isnan(value) ? 1 : 0;
}

void tl_log(const char* format, ...) {
    CodeContext* c = context();
    if (!c || !format) {
        return;
    }
    va_list args;
    va_start(args, format);
    QString text = vformat(format, args);
    va_end(args);
    if (!text.endsWith(QLatin1Char('\n'))) {
        text += QLatin1Char('\n');
    }
    c->appendLog(text);
}

int tl_printf(const char* format, ...) {
    CodeContext* c = context();
    if (!c || !format) {
        return 0;
    }
    va_list args;
    va_start(args, format);
    const QString text = vformat(format, args);
    va_end(args);
    c->appendLog(text);
    return int(text.toUtf8().size());
}

int tl_puts(const char* text) {
    CodeContext* c = context();
    if (c && text) {
        c->appendLog(QString::fromUtf8(text) + QLatin1Char('\n'));
    }
    return 0;
}

double tl_disturb(int channel) {
    CodeContext* c = context();
    return c && channel >= 0 && channel < c->disturbances.size() ? c->disturbances[channel] : 0.0;
}

void tl_set_iterations(int n) {
    if (CodeContext* c = context()) {
        c->iterations = n;
    }
}

void tl_predicted(const double* x, const double* y, const double* theta, int n) {
    CodeContext* c = context();
    if (!c) {
        return;
    }
    c->predictedX.clear();
    c->predictedY.clear();
    c->predictedTheta.clear();
    for (int i = 0; i < qMax(0, n); ++i) {
        if (x) {
            c->predictedX.append(x[i]);
        }
        if (y) {
            c->predictedY.append(y[i]);
        }
        if (theta) {
            c->predictedTheta.append(theta[i]);
        }
    }
}

// ---- dense linear algebra (row-major arrays) ----

using RowMatrix = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

// The products run on plain (column-major) Eigen matrices -- the same
// kernels, so the same summation order, as C++ code written with Eigen
// (the line follower's native MPC): a port to C gives the same numbers.
void tl_mat_mul(const double* a, const double* b, double* c, int n, int k, int m) {
    if (!a || !b || !c || n <= 0 || k <= 0 || m <= 0) {
        return;
    }
    const Eigen::MatrixXd ma = Eigen::Map<const RowMatrix>(a, n, k);
    Eigen::Map<RowMatrix> mc(c, n, m);
    if (m == 1) {
        const Eigen::VectorXd vb = Eigen::Map<const Eigen::VectorXd>(b, k);
        const Eigen::VectorXd r = ma * vb;
        mc = r;
        return;
    }
    const Eigen::MatrixXd mb = Eigen::Map<const RowMatrix>(b, k, m);
    const Eigen::MatrixXd r = ma * mb;
    mc = r;
}

void tl_mat_mul_tn(const double* a, const double* b, double* c, int n, int k, int m) {
    if (!a || !b || !c || n <= 0 || k <= 0 || m <= 0) {
        return;
    }
    const Eigen::MatrixXd ma = Eigen::Map<const RowMatrix>(a, k, n);
    Eigen::Map<RowMatrix> mc(c, n, m);
    if (m == 1) {
        const Eigen::VectorXd vb = Eigen::Map<const Eigen::VectorXd>(b, k);
        const Eigen::VectorXd r = ma.transpose() * vb;
        mc = r;
        return;
    }
    const Eigen::MatrixXd mb = Eigen::Map<const RowMatrix>(b, k, m);
    const Eigen::MatrixXd r = ma.transpose() * mb;
    mc = r;
}

int tl_mat_solve(const double* a, double* b, int n, int m) {
    if (!a || !b || n <= 0 || m <= 0) {
        return -1;
    }
    const Eigen::Map<const RowMatrix> ma(a, n, n);
    Eigen::Map<RowMatrix> mb(b, n, m);
    const Eigen::MatrixXd dense = ma;
    const Eigen::PartialPivLU<Eigen::MatrixXd> lu(dense);
    const Eigen::MatrixXd x = lu.solve(Eigen::MatrixXd(mb));
    if (!x.allFinite()) {
        return -1;
    }
    mb = x;
    return 0;
}

int tl_qp_box(const double* h, const double* g, const double* lb, const double* ub,
              const double* x0, double* x, int n) {
    if (!h || !g || !lb || !ub || !x || n <= 0) {
        return -1;
    }
    const Eigen::MatrixXd mh = Eigen::Map<const RowMatrix>(h, n, n);
    const Eigen::VectorXd vg = Eigen::Map<const Eigen::VectorXd>(g, n);
    const Eigen::VectorXd vlb = Eigen::Map<const Eigen::VectorXd>(lb, n);
    const Eigen::VectorXd vub = Eigen::Map<const Eigen::VectorXd>(ub, n);
    Eigen::VectorXd start;
    if (x0) {
        start = Eigen::Map<const Eigen::VectorXd>(x0, n);
    }
    const numeric::BoxQpResult result =
        numeric::solveBoxQp(mh, vg, vlb, vub, x0 ? &start : nullptr);
    Eigen::Map<Eigen::VectorXd>(x, n) = result.x;
    return result.iterations;
}

}  // extern "C"

// The C library's own functions, by address. Some are overloaded in C++,
// hence the casts to the C signatures tracelab.h declares.
using D1 = double (*)(double);
using D2 = double (*)(double, double);

}  // namespace

void CodeContext::restartNoise() {
    rngState = seed;
    hasSpareGaussian = false;
}

double CodeContext::uniform() {
    // splitmix64: tiny, fast and the same everywhere.
    rngState += 0x9E3779B97F4A7C15ULL;
    quint64 z = rngState;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return double(z >> 11) * (1.0 / 9007199254740992.0);
}

double CodeContext::gaussian() {
    // Box-Muller, the second value kept for the next call.
    if (hasSpareGaussian) {
        hasSpareGaussian = false;
        return spareGaussian;
    }
    double u1 = uniform();
    while (u1 <= 0.0) {
        u1 = uniform();
    }
    const double u2 = uniform();
    const double radius = std::sqrt(-2.0 * std::log(u1));
    spareGaussian = radius * std::sin(kTwoPi * u2);
    hasSpareGaussian = true;
    return radius * std::cos(kTwoPi * u2);
}

void CodeContext::appendLog(const QString& text) {
    partialLine += text;
    int newline = partialLine.indexOf(QLatin1Char('\n'));
    while (newline >= 0) {
        log.append(partialLine.left(newline));
        partialLine.remove(0, newline + 1);
        newline = partialLine.indexOf(QLatin1Char('\n'));
    }
    while (log.size() > kMaxLogLines) {
        log.removeFirst();
    }
}

CodeContext*& currentContext() {
    thread_local CodeContext* current = nullptr;
    return current;
}

void addHostSymbols(TCCState* state) {
    struct Symbol {
        const char* name;
        const void* address;
    };
    const Symbol symbols[] = {
        // math
        {"sin", reinterpret_cast<const void*>(D1(&std::sin))},
        {"cos", reinterpret_cast<const void*>(D1(&std::cos))},
        {"tan", reinterpret_cast<const void*>(D1(&std::tan))},
        {"asin", reinterpret_cast<const void*>(D1(&std::asin))},
        {"acos", reinterpret_cast<const void*>(D1(&std::acos))},
        {"atan", reinterpret_cast<const void*>(D1(&std::atan))},
        {"atan2", reinterpret_cast<const void*>(D2(&std::atan2))},
        {"sinh", reinterpret_cast<const void*>(D1(&std::sinh))},
        {"cosh", reinterpret_cast<const void*>(D1(&std::cosh))},
        {"tanh", reinterpret_cast<const void*>(D1(&std::tanh))},
        {"exp", reinterpret_cast<const void*>(D1(&std::exp))},
        {"log", reinterpret_cast<const void*>(D1(&std::log))},
        {"log10", reinterpret_cast<const void*>(D1(&std::log10))},
        {"log2", reinterpret_cast<const void*>(D1(&std::log2))},
        {"pow", reinterpret_cast<const void*>(D2(&std::pow))},
        {"sqrt", reinterpret_cast<const void*>(D1(&std::sqrt))},
        {"cbrt", reinterpret_cast<const void*>(D1(&std::cbrt))},
        {"hypot", reinterpret_cast<const void*>(D2(&std::hypot))},
        {"fabs", reinterpret_cast<const void*>(D1(&std::fabs))},
        {"floor", reinterpret_cast<const void*>(D1(&std::floor))},
        {"ceil", reinterpret_cast<const void*>(D1(&std::ceil))},
        {"round", reinterpret_cast<const void*>(D1(&std::round))},
        {"trunc", reinterpret_cast<const void*>(D1(&std::trunc))},
        {"fmod", reinterpret_cast<const void*>(D2(&std::fmod))},
        {"fmin", reinterpret_cast<const void*>(D2(&std::fmin))},
        {"fmax", reinterpret_cast<const void*>(D2(&std::fmax))},
        {"copysign", reinterpret_cast<const void*>(D2(&std::copysign))},
        {"ldexp", reinterpret_cast<const void*>(static_cast<double (*)(double, int)>(&std::ldexp))},
        {"frexp", reinterpret_cast<const void*>(static_cast<double (*)(double, int*)>(&std::frexp))},
        {"isnan_tl", reinterpret_cast<const void*>(&isnan_tl)},
        {"abs", reinterpret_cast<const void*>(static_cast<int (*)(int)>(&std::abs))},
        // memory and strings (memset/memcpy/memmove: tcc's own struct copies too)
        {"memset", reinterpret_cast<const void*>(&std::memset)},
        {"memcpy", reinterpret_cast<const void*>(&std::memcpy)},
        {"memmove", reinterpret_cast<const void*>(&std::memmove)},
        {"memcmp", reinterpret_cast<const void*>(&std::memcmp)},
        {"strlen", reinterpret_cast<const void*>(&std::strlen)},
        {"strcmp", reinterpret_cast<const void*>(&std::strcmp)},
        {"strncmp", reinterpret_cast<const void*>(&std::strncmp)},
        {"strcpy", reinterpret_cast<const void*>(&std::strcpy)},
        {"strncpy", reinterpret_cast<const void*>(&std::strncpy)},
        {"malloc", reinterpret_cast<const void*>(&std::malloc)},
        {"calloc", reinterpret_cast<const void*>(&std::calloc)},
        {"realloc", reinterpret_cast<const void*>(&std::realloc)},
        {"free", reinterpret_cast<const void*>(&std::free)},
        {"snprintf", reinterpret_cast<const void*>(&std::snprintf)},
        {"printf", reinterpret_cast<const void*>(&tl_printf)},
        {"puts", reinterpret_cast<const void*>(&tl_puts)},
        // TraceLab
        {"tl_ts", reinterpret_cast<const void*>(&tl_ts)},
        {"tl_fps", reinterpret_cast<const void*>(&tl_fps)},
        {"tl_tick", reinterpret_cast<const void*>(&tl_tick)},
        {"tl_time", reinterpret_cast<const void*>(&tl_time)},
        {"tl_preview_count", reinterpret_cast<const void*>(&tl_preview_count)},
        {"tl_noise", reinterpret_cast<const void*>(&tl_noise)},
        {"tl_rand", reinterpret_cast<const void*>(&tl_rand)},
        {"tl_nan", reinterpret_cast<const void*>(&tl_nan)},
        {"tl_log", reinterpret_cast<const void*>(&tl_log)},
        {"tl_predicted", reinterpret_cast<const void*>(&tl_predicted)},
        {"tl_set_iterations", reinterpret_cast<const void*>(&tl_set_iterations)},
        {"tl_disturb", reinterpret_cast<const void*>(&tl_disturb)},
        {"tl_mat_mul", reinterpret_cast<const void*>(&tl_mat_mul)},
        {"tl_mat_mul_tn", reinterpret_cast<const void*>(&tl_mat_mul_tn)},
        {"tl_mat_solve", reinterpret_cast<const void*>(&tl_mat_solve)},
        {"tl_qp_box", reinterpret_cast<const void*>(&tl_qp_box)},
    };
    for (const Symbol& symbol : symbols) {
        tcc_add_symbol(state, symbol.name, symbol.address);
    }
}

}  // namespace tracelab::code

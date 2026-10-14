#include "chartscale.h"

#include <QtMath>
#include <cmath>
#include <utility>

namespace traceview {

namespace {

// Candidate step multipliers, densest first. 2.5 is what gives an axis like
// 0/2.5/5/7.5/10 instead of jumping straight from a step of 2 to 5.
constexpr double kStepMultipliers[] = {1.0, 2.0, 2.5, 5.0, 10.0, 20.0};

// Ticks within this fraction of a step from zero print as exactly 0.
constexpr double kZeroSnap = 1e-9;

}  // namespace

NiceScale niceScale(double lo, double hi, int maxTicks, bool expand) {
    maxTicks = qMax(2, maxTicks);
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        lo = 0.0;
        hi = 1.0;
    }
    if (hi < lo) {
        std::swap(lo, hi);
    }
    if (hi - lo <= qMax(qAbs(lo), qAbs(hi)) * 1e-12) {
        const double pad = lo == 0.0 ? 1.0 : qAbs(lo) * 0.1;
        lo -= pad;
        hi += pad;
    }

    const double range = hi - lo;
    const double rawStep = range / (maxTicks - 1);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));

    NiceScale scale;
    for (double multiplier : kStepMultipliers) {
        const double step = multiplier * magnitude;
        const double eps = step * 1e-9;
        const double first =
            expand ? std::floor((lo + eps) / step) * step : std::ceil((lo - eps) / step) * step;
        const double last =
            expand ? std::ceil((hi - eps) / step) * step : std::floor((hi + eps) / step) * step;
        const long long count = std::llround((last - first) / step) + 1;
        if (count < 1 || count > maxTicks) {
            continue;
        }
        scale.step = step;
        scale.lo = expand ? first : lo;
        scale.hi = expand ? last : hi;
        scale.ticks.reserve(int(count));
        for (long long i = 0; i < count; ++i) {
            double value = first + double(i) * step;
            if (qAbs(value) < step * kZeroSnap) {
                value = 0.0;
            }
            scale.ticks.append(value);
        }
        scale.decimals = tickDecimals(step);
        return scale;
    }

    // Only reachable for a very short, non-expanded axis where no multiple of
    // any candidate step lands inside [lo, hi] -- mark just the two ends.
    scale.lo = lo;
    scale.hi = hi;
    scale.step = range;
    scale.ticks = {lo, hi};
    scale.decimals = tickDecimals(range);
    return scale;
}

int tickDecimals(double step) {
    step = qAbs(step);
    if (step == 0.0 || !std::isfinite(step)) {
        return 0;
    }
    double scaled = step;
    for (int decimals = 0; decimals < 9; ++decimals) {
        if (qAbs(scaled - std::round(scaled)) <= 1e-6 * qMax(1.0, qAbs(scaled))) {
            return decimals;
        }
        scaled *= 10.0;
    }
    return 9;
}

QString formatTick(double value, int decimals) {
    decimals = qBound(0, decimals, 9);
    if (qAbs(value) < 0.5 * std::pow(10.0, -decimals)) {
        value = 0.0;
    }
    return QString::number(value, 'f', decimals);
}

bool useScientificTicks(const QVector<double>& ticks, int decimals) {
    double largest = 0.0;
    for (double tick : ticks) {
        if (std::isfinite(tick)) {
            largest = qMax(largest, qAbs(tick));
        }
    }
    if (largest == 0.0) {
        return false;
    }
    return largest >= 1e5 || largest < 1e-2 || decimals > 3;
}

QStringList formatScientificTicks(const QVector<double>& ticks) {
    double largest = 0.0;
    for (double tick : ticks) {
        if (std::isfinite(tick)) {
            largest = qMax(largest, qAbs(tick));
        }
    }
    QStringList labels;
    if (largest == 0.0) {
        for (int i = 0; i < ticks.size(); ++i) {
            labels.append(QStringLiteral("0"));
        }
        return labels;
    }
    const int exponent = int(std::floor(std::log10(largest)));
    const double unit = std::pow(10.0, exponent);
    // Decimals for the mantissas: enough for the smallest gap between ticks.
    double step = 0.0;
    for (int i = 1; i < ticks.size(); ++i) {
        const double gap = qAbs(ticks[i] - ticks[i - 1]);
        if (gap > 0.0 && (step == 0.0 || gap < step)) {
            step = gap;
        }
    }
    const int decimals = qBound(1, step > 0.0 ? tickDecimals(step / unit) : 1, 4);
    for (double tick : ticks) {
        const double mantissa = tick / unit;
        if (qAbs(mantissa) < 0.5 * std::pow(10.0, -decimals)) {
            labels.append(QStringLiteral("0"));
            continue;
        }
        labels.append(QString::number(mantissa, 'f', decimals) + QLatin1Char('e') +
                      QString::number(exponent));
    }
    return labels;
}

QStringList formatTicks(const QVector<double>& ticks, int decimals, TickNumberFormat format) {
    const bool scientific =
        format == TickNumberFormat::Scientific ||
        (format == TickNumberFormat::Auto && useScientificTicks(ticks, decimals));
    if (scientific) {
        return formatScientificTicks(ticks);
    }
    QStringList labels;
    labels.reserve(ticks.size());
    for (double tick : ticks) {
        labels.append(formatTick(tick, decimals));
    }
    return labels;
}

int maxTicksFor(int lengthPx, int spacingPx) {
    if (spacingPx <= 0 || lengthPx <= 0) {
        return 2;
    }
    return qBound(2, lengthPx / spacingPx + 1, 11);
}

}  // namespace traceview

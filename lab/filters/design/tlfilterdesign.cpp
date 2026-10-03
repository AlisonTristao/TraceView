// tlfilterdesign.cpp -- see tlfilterdesign.h. The only file that includes Iir1.
#include "lab/filters/design/tlfilterdesign.h"

#include <Iir.h>

#include <cmath>
#include <exception>

namespace tlfd {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Copies Iir1's cascade (a0 kept apart by Iir1, the gain already folded into
// the first section by Iir1's Cascade::setLayout) into a0-normalized sections.
template <class Filter>
void extract(Filter& f, Design& d) {
    const int n = f.getNumStages();
    d.sections.clear();
    d.sections.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const Iir::Biquad& b = f[i];
        const double a0 = b.getA0();
        Section s;
        s.b0 = b.getB0() / a0;
        s.b1 = b.getB1() / a0;
        s.b2 = b.getB2() / a0;
        s.a1 = b.getA1() / a0;
        s.a2 = b.getA2() / a0;
        if (!std::isfinite(s.b0) || !std::isfinite(s.b1) || !std::isfinite(s.b2) ||
            !std::isfinite(s.a1) || !std::isfinite(s.a2)) {
            d.sections.clear();
            d.error = "the design produced non-finite coefficients";
            return;
        }
        d.sections.push_back(s);
    }
    d.ok = !d.sections.empty();
    if (!d.ok && d.error.empty()) d.error = "the design produced no section";
}

// Runs one Iir1 setup (normalized frequencies), turning its exceptions into
// Design::error.
template <class Filter, class Setup>
Design run(Setup&& setup) {
    Design d;
    try {
        Filter f;
        setup(f);
        extract(f, d);
    } catch (const std::exception& e) {
        d.ok = false;
        d.sections.clear();
        d.error = e.what();
    }
    return d;
}

// The family's four responses, by the base classes' normalized setup()
// (frequencies / sample rate): the same entry point for every family, unlike
// the user-facing setup()/setupN() overloads, whose argument lists differ.
template <class LP, class HP, class BP, class BS, class... Extra>
Design runFamily(const Spec& s, Extra... extra) {
    const double f = s.frequency / s.sampleRate;
    const double w = s.width / s.sampleRate;
    switch (s.response) {
        case Response::LowPass:
            return run<LP>([&](LP& x) { x.LP::IirBase::setup(s.order, f, extra...); });
        case Response::HighPass:
            return run<HP>([&](HP& x) { x.HP::IirBase::setup(s.order, f, extra...); });
        case Response::BandPass:
            return run<BP>([&](BP& x) { x.BP::IirBase::setup(s.order, f, w, extra...); });
        case Response::BandStop:
            return run<BS>([&](BS& x) { x.BS::IirBase::setup(s.order, f, w, extra...); });
    }
    Design d;
    d.error = "unknown response";
    return d;
}

// Iir1's filters, instantiated at kMaxOrder, each tagged with its base class
// so runFamily() can call the normalized setup() through it.
template <class Filter, class Base>
struct Tagged : Filter {
    using IirBase = Base;
};

namespace bw = Iir::Butterworth;
namespace c1 = Iir::ChebyshevI;
namespace c2 = Iir::ChebyshevII;

std::string validate(const Spec& s) {
    if (s.order < 1 || s.order > kMaxOrder)
        return "order must be between 1 and " + std::to_string(kMaxOrder);
    if (!(s.sampleRate > 0)) return "the sample rate must be positive";
    const double nyquist = s.sampleRate / 2;
    if (!(s.frequency > 0) || !(s.frequency < nyquist))
        return "the frequency must be between 0 and Nyquist (sampleRate / 2)";
    if (s.response == Response::BandPass || s.response == Response::BandStop) {
        if (!(s.width > 0)) return "the band width must be positive";
        if (!(s.frequency - s.width / 2 > 0) || !(s.frequency + s.width / 2 < nyquist))
            return "the band must lie between 0 and Nyquist";
    }
    if (s.family == Family::ChebyshevI && !(s.rippleDb > 0))
        return "the passband ripple must be positive";
    if (s.family == Family::ChebyshevII && !(s.stopbandDb > 0))
        return "the stopband attenuation must be positive";
    return {};
}

}  // namespace

Design design(const Spec& s) {
    const std::string err = validate(s);
    if (!err.empty()) {
        Design d;
        d.error = err;
        return d;
    }
    constexpr int kO = kMaxOrder;
    switch (s.family) {
        case Family::Butterworth:
            return runFamily<Tagged<bw::LowPass<kO>, bw::LowPassBase>,
                             Tagged<bw::HighPass<kO>, bw::HighPassBase>,
                             Tagged<bw::BandPass<kO>, bw::BandPassBase>,
                             Tagged<bw::BandStop<kO>, bw::BandStopBase>>(s);
        case Family::ChebyshevI:
            return runFamily<Tagged<c1::LowPass<kO>, c1::LowPassBase>,
                             Tagged<c1::HighPass<kO>, c1::HighPassBase>,
                             Tagged<c1::BandPass<kO>, c1::BandPassBase>,
                             Tagged<c1::BandStop<kO>, c1::BandStopBase>>(s, s.rippleDb);
        case Family::ChebyshevII:
            return runFamily<Tagged<c2::LowPass<kO>, c2::LowPassBase>,
                             Tagged<c2::HighPass<kO>, c2::HighPassBase>,
                             Tagged<c2::BandPass<kO>, c2::BandPassBase>,
                             Tagged<c2::BandStop<kO>, c2::BandStopBase>>(s, s.stopbandDb);
    }
    Design d;
    d.error = "unknown family";
    return d;
}

Design notch(double sampleRate, double center, double q) {
    Design d;
    if (!(sampleRate > 0) || !(center > 0) || !(center < sampleRate / 2) || !(q > 0)) {
        d.error = "notch: need sampleRate > 0, 0 < center < Nyquist and q > 0";
        return d;
    }
    try {
        Iir::RBJ::IIRNotch f;
        f.setupN(center / sampleRate, q);
        const double a0 = f.getA0();
        Section s;
        s.b0 = f.getB0() / a0;
        s.b1 = f.getB1() / a0;
        s.b2 = f.getB2() / a0;
        s.a1 = f.getA1() / a0;
        s.a2 = f.getA2() / a0;
        d.sections.push_back(s);
        d.ok = true;
    } catch (const std::exception& e) {
        d.error = e.what();
    }
    return d;
}

std::complex<double> response(const Design& d, double sampleRate, double f) {
    const double w = 2 * kPi * f / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);  // z^-1
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> h(1.0, 0.0);
    for (const Section& s : d.sections)
        h *= (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
    return h;
}

double magnitudeDb(const Design& d, double sampleRate, double f) {
    return 20.0 * std::log10(std::abs(response(d, sampleRate, f)));
}

}  // namespace tlfd

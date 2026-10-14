// tlfilterdesign.h -- host-side IIR design for the C filter runtime
// (lab/filters/runtime/tlfilter.h; lab/README.md, "Filters").
//
// The DESIGN half of lab/filters: C++17, desktop only, never shipped to the
// robot. It computes the second-order sections of a Butterworth, Chebyshev I
// or Chebyshev II low-pass, high-pass, band-pass or band-stop filter, or an
// RBJ notch, with the Iir1 library (github berndporr/iir1, MIT, fetched by
// lab/filters/CMakeLists.txt), and hands them to the RUNTIME half, the C99
// biquad cascade that runs the same numbers on the firmware.
//
// Bessel filters are not offered: Iir1 dropped them (with Elliptic, Legendre
// and the shelving pole filters it inherited from DSPFilters) in its 1.x line.
//
// Iir1 is an implementation detail of tlfilterdesign.cpp: nothing here
// includes it, and the sections come back as plain doubles.
#ifndef TRACELAB_FILTERS_TLFILTERDESIGN_H
#define TRACELAB_FILTERS_TLFILTERDESIGN_H

#include <complex>
#include <string>
#include <vector>

#include "lab/filters/runtime/tlfilter.h"

namespace tlfd {

enum class Family {
    Butterworth,  // maximally flat; -3 dB at the cutoff
    ChebyshevI,   // equiripple passband (rippleDb), sharper roll-off
    ChebyshevII,  // equiripple stopband (stopbandDb down), flat passband
};

enum class Response { LowPass, HighPass, BandPass, BandStop };

// What to design. Frequencies in Hz. LowPass/HighPass use `frequency` as the
// cutoff; BandPass/BandStop use it as the center and `width` as the band
// width. For ChebyshevI `frequency` is the passband edge (the response is
// -rippleDb there); for ChebyshevII it is the stopband edge (-stopbandDb).
struct Spec {
    Family family = Family::Butterworth;
    Response response = Response::LowPass;
    int order = 2;  // 1..kMaxOrder (band filters: 2*order poles, order sections)
    double sampleRate = 1000.0;
    double frequency = 50.0;
    double width = 0.0;
    double rippleDb = 1.0;     // ChebyshevI passband ripple
    double stopbandDb = 40.0;  // ChebyshevII stopband attenuation
};

constexpr int kMaxOrder = 16;

// One second-order section, a0 normalized to 1 (the runtime's convention):
// H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
struct Section {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct Design {
    bool ok = false;
    std::string error;              // why not, when !ok
    std::vector<Section> sections;  // in cascade order, gain folded in
};

// Designs `spec` with Iir1. Invalid parameters (order out of range, a
// frequency at or above Nyquist, a band crossing 0 or Nyquist...) come back as
// !ok with Iir1's message.
Design design(const Spec& spec);

// RBJ (Audio EQ Cookbook) notch at `center` Hz with quality factor q
// (bandwidth = center / q), Iir1's Iir::RBJ::IIRNotch: one section.
Design notch(double sampleRate, double center, double q = 10.0);

// Frequency response of the cascade at f Hz.
std::complex<double> response(const Design& d, double sampleRate, double f);
// 20 log10 |H(f)|.
double magnitudeDb(const Design& d, double sampleRate, double f);

// Fills the C runtime's cascade (tlf_biquad_init) with the design, converted
// to tlf_real. Header-inline so it is compiled with the caller's tlf_real
// (float, or double with TLF_USE_DOUBLE). Returns TLF_OK, or TLF_EINVAL when
// the design is not ok or has more than TLF_BIQUAD_MAX_SECTIONS sections.
inline int toRuntime(const Design& d, tlf_biquad* out) {
    if (!d.ok || d.sections.empty() || d.sections.size() > TLF_BIQUAD_MAX_SECTIONS)
        return TLF_EINVAL;
    tlf_biquad_coeffs c[TLF_BIQUAD_MAX_SECTIONS];
    for (size_t i = 0; i < d.sections.size(); ++i) {
        const Section& s = d.sections[i];
        c[i].b0 = static_cast<tlf_real>(s.b0);
        c[i].b1 = static_cast<tlf_real>(s.b1);
        c[i].b2 = static_cast<tlf_real>(s.b2);
        c[i].a1 = static_cast<tlf_real>(s.a1);
        c[i].a2 = static_cast<tlf_real>(s.a2);
    }
    return tlf_biquad_init(out, c, static_cast<uint8_t>(d.sections.size()));
}

}  // namespace tlfd

#endif  // TRACELAB_FILTERS_TLFILTERDESIGN_H

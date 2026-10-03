#pragma once

// The line follower's plant as a BTP device: the topics and actions of
// lab/plant/plantmanifest.h (names) and of the `loop` profile (BTP
// docs/profiles/loop.md), with their fields, ids and units. Consumers bind
// by name; the ids below are this producer's own choice.
//
// Telemetry:
//   loop.state   every tick: tick, mode, then the measurements -- w_l, w_r
//                (encoders, rad/s), gyro (rad/s, > 0 turns right) and s[]
//                (the line array, 0..1, s0 leftmost) -- as the car's sensors
//                report them (lfsensors.h, noise included when it is on).
//   loop.output  every tick: ref_tick, source, u_l, u_r (% applied).
//   sim.truth    tick, t, x, y, theta, next_point (index into sim.track's
//                points), coverage (%), lateral_error (m), speed, omega.
//   sim.states   tick and the true state: w_l, w_r, omega, x, y, theta, plus
//                the perturbation in effect and, for comparison, the
//                encoders' w_l_meas, w_r_meas.
//   sim.preview  tick, px[], py[]: the future track points in the car frame
//                of that tick (x lateral right, y forward). Published before
//                loop.state of the same tick.
//   sim.params   the run's settings, on subscribe and on change.
//   sim.track    UTF8 {"points": [[x, y], ...], "closed": true, "stride": k}
//                in metres: every k-th point of the track (so the document
//                fits one BTP message: at most 255 fragments of 210
//                octets), on subscribe and after a reset.
// Actions:
//   loop.u       STREAM: ref_tick, u_l[], u_r[] (%, -100..100). Element j is
//                for tick ref_tick + 1 + j. Float64 rather than the profile's
//                float32, so the loop through BTP is bit for bit the
//                in-process harness.
//   loop.mode    DANGEROUS: mode (OFF/LOCAL/REMOTE/INJECT), arm.
//   loop.config  IDEMPOTENT: seed, track_type, sensor_noise, sensor_seed --
//                applied on the next sim.reset.
//   sim.run, sim.pause, sim.reset, sim.step (no parameters),
//   sim.disturb  perturbation (%, left +q, right -q), from the next tick,
//   sim.speed    run_mode (0 real time, 1 as fast as possible, 2 lockstep),
//                time_scale (real time only).
// The text command action (id 1, "run this line", what TraceView's
// Backend::sendCommand() and the Scene widget's keys send) takes the same
// names: "sim.run", "sim.disturb 5", "sim.speed 2", "sim.speed fast",
// "sim.speed lockstep", "loop.mode remote".

#include <QtGlobal>
#include <btp/catalog.hpp>

namespace tracelab::lf {

namespace topic {
inline constexpr quint16 kLoopState = 0x0001;
inline constexpr quint16 kLoopOutput = 0x0002;
inline constexpr quint16 kSimTruth = 0x0003;
inline constexpr quint16 kSimStates = 0x0004;
inline constexpr quint16 kSimPreview = 0x0005;
inline constexpr quint16 kSimParams = 0x0006;
inline constexpr quint16 kSimTrack = 0x0007;
}  // namespace topic

namespace action {
inline constexpr quint16 kText = 0x0001;
inline constexpr quint16 kLoopU = 0x0010;
inline constexpr quint16 kLoopMode = 0x0011;
inline constexpr quint16 kLoopConfig = 0x0012;
inline constexpr quint16 kSimRun = 0x0013;
inline constexpr quint16 kSimPause = 0x0014;
inline constexpr quint16 kSimReset = 0x0015;
inline constexpr quint16 kSimStep = 0x0016;
inline constexpr quint16 kSimDisturb = 0x0017;
inline constexpr quint16 kSimSpeed = 0x0018;
}  // namespace action

// The line array's size and the longest horizon sim.preview / loop.u carry.
inline constexpr int kLineSensors = 15;
inline constexpr int kMaxPreview = 256;
inline constexpr int kMaxPlan = 64;

using PlantCatalog = btp::StaticCatalog</*Topics=*/8, /*Fields=*/96, /*StringBytes=*/4096,
                                        /*SourceInfoEntries=*/0, /*Actions=*/12>;

// Fills an empty catalogue with the topics and actions above. False when a
// pool is too small (a programming error, caught by the tests).
bool buildPlantCatalog(btp::Catalog& catalog);

}  // namespace tracelab::lf

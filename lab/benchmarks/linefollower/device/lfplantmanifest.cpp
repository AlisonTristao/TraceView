#include "lfplantmanifest.h"

#include <btp/messages.hpp>
#include <btp/telemetry.hpp>

#include "lab/plant/plantmanifest.h"

namespace tracelab::lf {

namespace {

btp::FieldRecord array(btp::FieldRecord field, int maxCount) {
    field.flags = std::uint8_t(field.flags | btp::kFieldVariableCount);
    field.element_count = 0;
    field.max_element_count = std::uint16_t(maxCount);
    return field;
}

btp::FieldRecord ranged(btp::FieldRecord field, double minimum, double maximum) {
    return btp::range(field, minimum, maximum);
}

bool ok(btp::MessageError error) {
    return error == btp::MessageError::Ok;
}

// No rate cap: a viewer asks what it wants; the plant publishes at most once
// a tick anyway.
constexpr std::uint32_t kNoCap = 0U;

}  // namespace

bool buildPlantCatalog(btp::Catalog& catalog) {
    using btp::TelemetryEncoding;
    const btp::FieldRecord loopState[] = {
        btp::u32("tick"),
        btp::enum8("mode"),
        btp::f64("w_l", "rad/s"),
        btp::f64("w_r", "rad/s"),
        btp::f64("gyro", "rad/s"),
        ranged(array(btp::f64("s", "1"), kLineSensors), 0.0, 1.0),
    };
    const btp::FieldRecord loopOutput[] = {
        btp::u32("ref_tick"),
        btp::enum8("source"),
        ranged(btp::f64("u_l", "%"), -100.0, 100.0),
        ranged(btp::f64("u_r", "%"), -100.0, 100.0),
    };
    const btp::FieldRecord simTruth[] = {
        btp::u32("tick"),
        btp::f64("t", "s"),
        btp::f64("x", "m"),
        btp::f64("y", "m"),
        btp::f64("theta", "rad"),
        btp::u32("next_point"),
        ranged(btp::f64("coverage", "%"), 0.0, 100.0),
        btp::f64("lateral_error", "m"),
        btp::f64("speed", "m/s"),
        btp::f64("omega", "rad/s"),
    };
    const btp::FieldRecord simStates[] = {
        btp::u32("tick"),
        btp::f64("w_l", "rad/s"),
        btp::f64("w_r", "rad/s"),
        btp::f64("omega", "rad/s"),
        btp::f64("x", "m"),
        btp::f64("y", "m"),
        btp::f64("theta", "rad"),
        btp::f64("perturbation", "%"),
        // The encoders' reading next to the true speeds, for one chart.
        btp::f64("w_l_meas", "rad/s"),
        btp::f64("w_r_meas", "rad/s"),
    };
    const btp::FieldRecord simPreview[] = {
        btp::u32("tick"),
        array(btp::f64("px", "m"), kMaxPreview),
        array(btp::f64("py", "m"), kMaxPreview),
    };
    const btp::FieldRecord simParams[] = {
        btp::f64("ts", "s"),
        btp::i64("seed"),
        btp::enum8("track_type"),
        btp::boolean("sensor_noise"),
        btp::u32("sensor_seed"),
        btp::enum8("run_mode"),
        btp::f64("time_scale", "1"),
        btp::u32("horizon"),
        btp::f64("wheel_distance", "m"),
    };
    if (!ok(catalog.add_topic(topic::kLoopState, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                              kTopicLoopState, loopState, std::size(loopState))) ||
        !ok(catalog.add_topic(topic::kLoopOutput, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                              kTopicLoopOutput, loopOutput, std::size(loopOutput))) ||
        !ok(catalog.add_topic(topic::kSimTruth, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                              kTopicSimTruth, simTruth, std::size(simTruth))) ||
        !ok(catalog.add_topic(topic::kSimStates, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                              kTopicSimStates, simStates, std::size(simStates))) ||
        !ok(catalog.add_topic(topic::kSimPreview, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                              kTopicSimPreview, simPreview, std::size(simPreview))) ||
        !ok(catalog.add_topic(topic::kSimParams, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                              kTopicSimParams, simParams, std::size(simParams))) ||
        !ok(catalog.add_topic(topic::kSimTrack, 1, TelemetryEncoding::Utf8, true, kNoCap,
                              kTopicSimTrack, nullptr, 0))) {
        return false;
    }

    using btp::ActionEncoding;
    const btp::FieldRecord loopU[] = {
        btp::u32("ref_tick"),
        ranged(array(btp::f64("u_l", "%"), kMaxPlan), -100.0, 100.0),
        ranged(array(btp::f64("u_r", "%"), kMaxPlan), -100.0, 100.0),
    };
    const btp::FieldRecord loopMode[] = {btp::enum8("mode"), btp::boolean("arm")};
    const btp::FieldRecord loopConfig[] = {
        btp::i64("seed"),
        btp::enum8("track_type"),
        btp::boolean("sensor_noise"),
        btp::u32("sensor_seed"),
    };
    const btp::FieldRecord disturb[] = {ranged(btp::f64("perturbation", "%"), -100.0, 100.0)};
    const btp::FieldRecord speed[] = {btp::enum8("run_mode"), btp::f64("time_scale", "1")};
    const auto none = [&](quint16 id, const char* name, const char* description) {
        return ok(catalog.add_action(id, 1, 0, ActionEncoding::Empty, name, nullptr, 0, nullptr,
                                     0, ActionEncoding::Empty, 0, description));
    };
    return ok(catalog.add_action(action::kLoopU, 1, btp::kActionStream, ActionEncoding::PackedLe,
                                 kActionLoopU, loopU, std::size(loopU), nullptr, 0,
                                 ActionEncoding::Empty, 0,
                                 "Wheel commands for the next ticks")) &&
           ok(catalog.add_action(action::kLoopMode, 1, btp::kActionDangerous,
                                 ActionEncoding::PackedLe, kActionLoopMode, loopMode,
                                 std::size(loopMode), nullptr, 0, ActionEncoding::Empty, 0,
                                 "Loop mode", "Let the controller drive the car?")) &&
           ok(catalog.add_action(action::kLoopConfig, 1, btp::kActionIdempotent,
                                 ActionEncoding::PackedLe, kActionLoopConfig, loopConfig,
                                 std::size(loopConfig), nullptr, 0, ActionEncoding::Empty, 0,
                                 "Track and sensors, applied on the next reset")) &&
           none(action::kSimRun, kActionSimRun, "Run") &&
           none(action::kSimPause, kActionSimPause, "Pause") &&
           none(action::kSimReset, kActionSimReset, "Back to the start") &&
           none(action::kSimStep, kActionSimStep, "One tick") &&
           ok(catalog.add_action(action::kSimDisturb, 1, 0, ActionEncoding::PackedLe,
                                 kActionSimDisturb, disturb, std::size(disturb), nullptr, 0,
                                 ActionEncoding::Empty, 0, "Motor perturbation")) &&
           ok(catalog.add_action(action::kSimSpeed, 1, 0, ActionEncoding::PackedLe, "sim.speed",
                                 speed, std::size(speed), nullptr, 0, ActionEncoding::Empty, 0,
                                 "Real time, as fast as possible or lockstep"));
}

}  // namespace tracelab::lf

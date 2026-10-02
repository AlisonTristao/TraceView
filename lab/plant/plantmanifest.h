#pragma once

#include "lab/sim/modeldefinition.h"

namespace btp {
class Catalog;
}

namespace tracelab {

// The names the simulated plant publishes under (plan 4.3). They are the
// binding: TraceView widgets and the TraceLab scenes find these topics and
// their fields by name (docs/DASHBOARD.md "Telemetry bindings"), so the
// same dashboard works against the real robot, which publishes loop.* too.
//
// Telemetry topics, built from the ModelDefinition, no hand-written schema:
//   loop.state   every tick: tick, then one field per output (y[], model
//                order; an output of size n is an array field of n). What the
//                controller sees.
//   loop.output  every tick: tick and the input really applied (after
//                saturation), one field per input.
//   sim.states   every tick: tick and one field per state.
//   sim.truth    every tick, evaluation only: tick, t, the true pose x, y,
//                theta and, with a track, next_point and coverage.
//   sim.preview  optional, every tick, privileged: tick, px[], py[] (see
//                TrackEnvironment in lab/sim/environment.h). Sent BEFORE
//                loop.state of the same tick, so the controller host already
//                holds it when that loop.state arrives.
//   sim.params   on change: one field per parameter.
//   sim.track    UTF8 document (UTF8, not JSON_UTF8: TraceView routes only
//                UTF8 bodies to widgets), on connect and on change:
//                {"points": [[x, y], ...], "closed": true} in metres.
// Actions (commands with typed arguments):
//   loop.u       STREAM: ref_tick plus the input horizon, each input with the
//                model's min/max as its declared range.
//   loop.mode    DANGEROUS, needs "arm": open/closed loop.
//   loop.config  set a parameter by name, live.
//   sim.run, sim.pause, sim.reset, sim.step, sim.disturb (a perturbation in
//   % added to the inputs; the line-follower scene's P/O/R keys).
inline constexpr char kTopicLoopState[] = "loop.state";
inline constexpr char kTopicLoopOutput[] = "loop.output";
inline constexpr char kTopicSimStates[] = "sim.states";
inline constexpr char kTopicSimTruth[] = "sim.truth";
inline constexpr char kTopicSimPreview[] = "sim.preview";
inline constexpr char kTopicSimParams[] = "sim.params";
inline constexpr char kTopicSimTrack[] = "sim.track";
inline constexpr char kActionLoopU[] = "loop.u";
inline constexpr char kActionLoopMode[] = "loop.mode";
inline constexpr char kActionLoopConfig[] = "loop.config";
inline constexpr char kActionSimRun[] = "sim.run";
inline constexpr char kActionSimPause[] = "sim.pause";
inline constexpr char kActionSimReset[] = "sim.reset";
inline constexpr char kActionSimStep[] = "sim.step";
inline constexpr char kActionSimDisturb[] = "sim.disturb";

// Fills `catalog` (empty, caller-owned pools) with the topics above for
// `model`. Returns false if the pools are too small.
//
// The actions cannot be declared yet: the BTP TraceView pins (2.48.0) has no
// action records in its Catalog. BTP 2.50 (STREAM flag, actions in the
// Catalog, Node::send_stream(), the loop profile) exists only locally, on
// the feature/stream-actions branch; once it is published and pinned, this
// adds the actions as well.
bool buildPlantCatalog(const sim::ModelDefinition& model, btp::Catalog& catalog);

}  // namespace tracelab

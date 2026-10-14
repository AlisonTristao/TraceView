#pragma once

namespace tracelab::lf {

// Registers the line follower's code templates (lab/benchmarks/linefollower/
// code: the controllers, filters and plant in C) with code::CodeLibrary, as
// "linefollower/<name>". Safe to call more than once.
void registerLineFollowerTemplates();

}  // namespace tracelab::lf

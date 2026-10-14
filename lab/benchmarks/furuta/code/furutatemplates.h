#pragma once

namespace tracelab::furuta {

// Registers the Furuta pendulum's code templates (lab/benchmarks/furuta/code:
// the plant and its controllers in C) with code::CodeLibrary, as
// "furuta/<name>". Safe to call more than once.
void registerFurutaTemplates();

}  // namespace tracelab::furuta

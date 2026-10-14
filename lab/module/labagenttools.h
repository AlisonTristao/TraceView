#pragma once

namespace traceview {
class ModuleHost;
class ToolRegistry;
}  // namespace traceview

namespace tracelab {

// TraceLab's tools for AI assistants (docs/MCP.md): the lab's plants,
// controllers and filters, their C code blocks (read, write and compile,
// parameters, templates), the simulation and its clock, the plant's
// settings, scenes and the benchmark projects. They act on the same devices
// the TraceLab tab shows (LabDeviceRegistry), so the tab follows along.
void registerLabAgentTools(traceview::ModuleHost* host, traceview::ToolRegistry* registry);

}  // namespace tracelab

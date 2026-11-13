#pragma once

#include <QString>

namespace traceview {

class MainWindow;
class ToolRegistry;

// The tools AI assistants use to drive the main window over MCP (see
// docs/MCP.md). A friend of MainWindow, so each tool runs the same private
// operation its menu or panel does -- the UI refreshes exactly as if the user
// had done it, and edits land on the same undo stacks. Split by area:
// agentapptools.cpp (state, workspaces, dashboard, project),
// agentdevicetools.cpp (devices, links, commands, OTA) and
// agentpreferencetools.cpp (appearance, language, settings, diagnostics).
class AgentAppTools {
public:
    static void registerTools(MainWindow* window, ToolRegistry* registry);

    // Asks the user in a dialog; true when they accept. For tool actions
    // undo can't take back.
    static bool confirm(MainWindow* window, const QString& title, const QString& text);
    // Asks once per bridge session before an assistant first touches real
    // hardware (connecting, commands, scripts): true when the user allowed
    // it now or earlier in this session. `what` names the action asked for.
    static bool allowHardware(MainWindow* window, const QString& what);

private:
    static void registerStateTools(MainWindow* window, ToolRegistry* registry);
    static void registerWorkspaceTools(MainWindow* window, ToolRegistry* registry);
    static void registerWidgetTools(MainWindow* window, ToolRegistry* registry);
    static void registerDashboardViewTools(MainWindow* window, ToolRegistry* registry);
    static void registerProjectTools(MainWindow* window, ToolRegistry* registry);
    static void registerDeviceTools(MainWindow* window, ToolRegistry* registry);
    static void registerDeviceIoTools(MainWindow* window, ToolRegistry* registry);
    static void registerOtaTools(MainWindow* window, ToolRegistry* registry);
    static void registerAppearanceTools(MainWindow* window, ToolRegistry* registry);
    static void registerSettingsTools(MainWindow* window, ToolRegistry* registry);
    static void registerDiagnosticsTools(MainWindow* window, ToolRegistry* registry);
};

}  // namespace traceview

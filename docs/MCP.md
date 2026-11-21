# AI assistants (MCP)

TraceView can be driven by AI assistants such as Claude through the
[Model Context Protocol](https://modelcontextprotocol.io). An assistant
connected this way works on the TraceView window you are looking at: it
creates workspaces, places and configures widgets, reads telemetry, takes
screenshots to check its own work and saves the project. Its dashboard edits
go on the same undo stack as yours.

## Connecting

1. In TraceView, turn on **Settings > Connections > AI assistants**. It is
   off by default; turning it on is what allows an assistant in.
2. Register TraceView as an MCP server once. The Settings section shows both
   registrations below with this computer's exact paths, each with a
   **Copy** button. Together they cover every Claude on the computer.

   **Claude Code** (terminal, VS Code / JetBrains extensions, and the Code tab
   of the Claude desktop app) -- run once in a terminal:

   ```sh
   claude mcp add --scope user traceview -- "C:\Program Files\TraceView\TraceView.exe" --mcp
   ```

   `--scope user` registers it for every folder. Without it, `claude mcp add`
   uses the `local` scope, which only applies to the folder you ran it in.
   A folder-scoped entry with the same name wins over the user one, so remove
   old ones with `claude mcp remove traceview` from that folder if a project
   keeps launching a different build.

   **Claude desktop app (chat)** reads its own `claude_desktop_config.json`
   (Settings > Developer > Edit Config), which `claude mcp` doesn't touch.
   Add the entry inside its `mcpServers` object and restart the app:

   ```json
   {
     "mcpServers": {
       "traceview": {
         "command": "C:\\Program Files\\TraceView\\TraceView.exe",
         "args": ["--mcp"]
       }
     }
   }
   ```

   Any other MCP client that launches stdio servers works the same way: the
   command is the TraceView executable with `--mcp`.

The status bar shows when an assistant connects or disconnects.

If TraceView is closed when the assistant starts, the assistant sees a single
`start_traceview` tool. It opens TraceView with assistant access on for that
run only (the `--agent-bridge` flag), whatever the Settings toggle says. The
full tool list then replaces it.

On Linux the command depends on how TraceView is installed, and the Settings
section always shows the right one:

- **AppImage**: the `.AppImage` file itself, e.g.
  `claude mcp add --scope user traceview -- ~/Apps/TraceView-4.9.0-linux-x64.AppImage --mcp`.
- **Flatpak**: `claude mcp add --scope user traceview -- flatpak run io.github.alisontristao.TraceView --mcp`.
  The sandbox can't start a TraceView that outlives it, so `start_traceview`
  only asks the user to open TraceView.
- **Built from source / distribution package**: the `TraceView` binary.

## How it works

```
MCP client (Claude Code / Desktop)
      │  stdio, newline-delimited JSON-RPC
TraceView.exe --mcp                 lib/agent/mcpstdiorelay.cpp
      │  local socket, same framing
TraceView (the running window)      lib/agent/agentbridgeserver.cpp
  └─ McpSession → ToolRegistry      lib/agent/mcpsession.cpp, toolregistry.cpp
        └─ tools registered by each feature (core/agentapptools.cpp, ...)
```

- `TraceView --mcp` doesn't open a window. It is a relay between the
  client's stdin/stdout and the running app. It answers `initialize` and
  `ping` itself, so the client can connect before the app is up. It sends
  `notifications/tools/list_changed` when the app comes or goes, and fails
  any call still waiting when the app closes, so the client never hangs.
- The app listens on a per-user local socket: a named pipe on Windows, a Unix
  socket in `$XDG_RUNTIME_DIR` elsewhere (Flatpak: the app's shared runtime
  directory). The socket is created owner-only, so other accounts on the
  machine can't connect. Only one TraceView window serves at a time; a second
  one reports that another already does.
- Every tool runs on the GUI thread through the same code path as its menu
  or panel, so the UI updates live. Dashboard tools push the existing
  `QUndoCommand`s, one undo step per call.
- A failed call returns a normal result with `isError` and a message, so the
  model can correct itself. JSON-RPC errors are only for protocol faults.
- Anything that can't be undone (`delete_workspace`) asks the user in a
  dialog first.

## Tools

| Area | Tools |
|---|---|
| State | `get_app_state` (version, project, workspaces, devices, layout), `screenshot` (dashboard, window or one widget) |
| Telemetry | `list_signals` (each device's topics and fields), `read_signal` (recent samples and stats of a plotted field), `get_subscriptions` (requested/granted rates, what the device publishes) |
| Workspaces | `get_workspace`, `create_workspace`, `switch_workspace`, `update_workspace`, `delete_workspace`¹ |
| Widgets | `list_widget_types` (config shape of each type), `add_widget`, `update_widget`, `remove_widgets`, `arrange_widget` (stacking), `group_widgets`, `ungroup_widgets`, `select_widget` |
| Dashboard view | `set_layout` (large/medium/small), `resize_canvas`, `set_grid_precision` (1×/2×/4× snap lines per cell), `set_edit_mode`, `show_view` (dashboard, devices, subscriptions, OTA, BTP monitor, notifications, settings), `undo`/`redo` (dashboard or device history) |
| Project | `list_projects` (open file, recent, gallery), `save_project`, `new_project`¹, `open_project`¹, `clear_recent_projects`, `open_gallery_dashboard`¹, `save_to_gallery`, `set_gallery_default`, `rename_gallery_dashboard`, `export_gallery_dashboard`³, `delete_gallery_dashboard`¹ |
| Devices | `get_device`, `add_device`², `update_device`², `remove_device`, `connect_device`², `list_ports` (serial, USB HID, hub robots, BLE scan) |
| Talking to devices | `send_command`², `send_terminal`², `read_terminal`, `read_device_log`, `get_device_script`, `set_device_script`² |
| Firmware | `ota_status`, `ota_upload`¹, `ota_reboot_to_app`² (leave the recovery without flashing: the OTA page's "To app") |
| TraceKit panel | `tracekit_get_settings`², `tracekit_set_settings`², `tracekit_reset_setting`² (what the device declares with `cfg -json`), `tracekit_wifi_list`², `tracekit_wifi_add`² (the password goes to the device only, never to the terminal or log), `tracekit_wifi_remove`¹², `tracekit_action`² (set_clock, status, keep_awake, reboot, recovery, usb_bootloader, factory_reset¹) |
| Appearance | `get_appearance`, `set_appearance`, `apply_appearance_preset`, `save_appearance_preset`, `delete_appearance_preset`¹, `pin_workspace_appearance`, `create_palette`, `update_palette`, `delete_palette`¹, `set_language`, `set_font` |
| Settings | `get_settings`, `update_settings` (everything in the Settings window except appearance), `clear_manifest_cache`, `check_for_updates` (installing stays the user's choice) |
| Diagnostics | `read_notifications`, `clear_notifications`, `read_app_log`, `read_btp_traffic`, `export_btp_traffic`³ (JSON Lines or CSV, full payloads), `set_btp_monitor_paused`, `open_log_file` (a recorded `.blog` in its own tab) |

¹ Asks the user in a dialog every time: these can't be undone, or replace
unsaved work, or flash firmware.
² Real hardware. The first time an assistant connects a device, sets up how
one is reached (TraceView dials a configured link on its own), sends it a
command or runs a device script, TraceView asks the user to allow hardware
access. The answer holds until assistant access is turned off or TraceView
closes.
³ Writing over an existing file asks the user first.

None of these questions comes up once the user ticks **Allow everything
without asking** (Settings > Connections > AI assistants) or answers one of
them with **Yes to All**; unticking it brings them back. Like assistant
access itself, this is not in `update_settings`: only the user can grant it.

Not exposed on purpose: logging in to Developer mode and managing users
(an assistant must not be able to elevate itself), and turning assistant
access itself on or off.

Widget positions go over the wire in grid cells: columns across the canvas
width and rows per page (one viewport height) of each layout (`large`,
`medium`, `small`). `get_workspace` lists the grid.

## Adding tools

See CONTRIBUTING.md, "Exposing features to AI assistants".

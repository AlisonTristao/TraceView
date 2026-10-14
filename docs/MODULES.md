# Optional application modules

TraceView is a telemetry tool first. An optional module adds a larger
feature on top of it without that feature showing up for people who only
use telemetry: it is compiled into the executable, but stays off until it is
enabled. The contract lives in [lib/modules](../lib/modules) (target
`traceview_modules`, QtCore only, so a module library never depends on
`traceview_ui`).

## The pieces

- **`AppModule`** ([appmodule.h](../lib/modules/appmodule.h)) is what a
  module implements:
  - `id()`: stable and never translated, for example `"lab"`. It is the
    QSettings key and the `--mode` value, so it must not change.
  - `displayName()` and `description()`: translated with
    `QCoreApplication::translate()`. They are read after the translator is
    installed, never at registration time.
  - `install(ModuleHost&)`: called once while the main window is built, and
    only when the module is enabled.
- **`ModuleHost`** (same header) is what `install()` receives; `MainWindow`
  implements it. It offers these hooks:
  - `widgetRegistry()`: the dashboard's `WidgetRegistry`, to register the
    module's widget types (see [DASHBOARD.md](DASHBOARD.md)). Install runs
    before the startup dashboard loads, so saved projects can use them.
    A module widget that shows telemetry implements `TelemetryConsumer`
    ([telemetryconsumer.h](../lib/dashboard/telemetryconsumer.h)) next to
    `DashboardWidget`: MainWindow cannot know it by type, so it subscribes
    the topics the widget lists and forwards their samples (see
    [DASHBOARD.md](DASHBOARD.md), "Telemetry bindings").
  - `addRibbonTab(label, actions, content)`: one fixed ribbon tab. Its page
    holds one button group made from `actions`, and `content` (owned by the
    window from then on) fills the area under the ribbon while the tab is
    current. Parent the actions to `content`.
  - `registerDeviceType(type)`: an in-process device kind
    ([moduledevicetype.h](../lib/core/moduledevicetype.h)), offered by Add
    Device and saved in projects with its `moduleConfig` only.
  - `openProject(path)`: opens a project file or Qt resource, as File > Open.
  - `registerExampleProject(group, name, path)`: a project the module ships
    (a Qt resource), listed under File > Open Example in `group`. Saving one
    asks for a file. TraceLab lists each benchmark this way.
  - `addModuleDevice(typeId, name, config)` and
    `addDashboardWidget(typeId, config)`: add a device of a registered type
    to the open project, or a widget to the dashboard -- what a module's own
    build tools use (TraceLab's Add plant, Add controller, Add scene).
- **`ModuleRegistry`** ([moduleregistry.h](../lib/modules/moduleregistry.h))
  holds the modules this executable offers and knows which are enabled.

There is no unload. Turning a module on or off takes effect after a
restart, like the language setting.

## Registering a module

`src/main.cpp` registers modules on `ModuleRegistry::instance()` before it
reads `--mode` and before `MainWindow` is constructed:

```cpp
traceview::ModuleRegistry::instance().registerModule(std::make_unique<MyModule>());
traceview::ModuleRegistry::instance().applyModeArgument(QCoreApplication::arguments());
traceview::MainWindow window;
```

A module that ships in its own folder adds the registration behind its own
CMake option and links its library into the `TraceView` target. The one
module today is TraceLab ([lab/](../lab/README.md), id `lab`): option
`TRACEVIEW_ENABLE_LAB` (on by default, forced off on Android and iOS) adds
`lab/`, links `tracelab_module` and defines `TRACEVIEW_ENABLE_LAB` for
`main.cpp` only, which then registers `tracelab::LabModule`. With the option
off the executable has no module and behaves exactly as before. `lab/`
targets may link `lib/` ones; no `lib/` target links a `lab/` one.

## Enabling

- **Settings > General > Modules** lists every registered module with a
  checkbox and its description. The choice is saved as
  `modules/<id>/enabled` in QSettings (off by default) and applies on the
  next start. The section does not appear when no module is registered.
- **`--mode <id>`** (or `--mode=<id>`) enables that module for this run
  only, without changing the saved choice, and gives the app the module's
  identity: the window title and the User-mode corner wordmark show its
  `displayName()` instead of "TraceView". QSettings and the log folder keep
  TraceView's name, so both identities share one configuration. An unknown
  id, or `--mode` without one, is ignored with a warning in the log file.

A desktop shortcut that runs `TraceView --mode <id>` gives the module its
own entry point in the same installation. TraceLab ships one: a "TraceLab"
Start Menu entry in the NSIS installer (`CPACK_NSIS_CREATE_ICONS_EXTRA` in
the root `CMakeLists.txt`) and
`resources/linux/io.github.alisontristao.TraceView.TraceLab.desktop` on
Linux. Both use TraceView's icon. The AppImage keeps TraceView's own
`.desktop` file as its entry (named explicitly in
`scripts/build_linux_appimage.sh`).

## Tests

`tests/test_moduleregistry.cpp` covers registration, the saved enabled
state, `--mode` and installing only the enabled modules against a fake
host. `tests/test_mainwindowmodules.cpp` checks the real `MainWindow`
installing a fake module (widget type, ribbon tab, content swap, title) and
the Settings section.

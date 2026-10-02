# TraceLab

TraceLab is TraceView's simulation and control mode: describe a plant by its
state equations, simulate it with a recognised solver, close the loop with a
controller you program and watch it on the dashboards and in an animated
scene. It is an optional module ([docs/MODULES.md](../docs/MODULES.md), id
`lab`), compiled in when `TRACEVIEW_ENABLE_LAB` is ON (the default; always OFF
on Android and iOS) and switched on in **Settings > General > Modules** or for
one run with `TraceView --mode lab`, which is what the **TraceLab** launcher
does.

The plant and the controller are BTP endpoints that speak the `loop` profile,
even inside one process (a loopback transport), so either one can be swapped
for the real robot or the firmware without the other noticing.

This folder is the skeleton: the contracts are written, the presentation side
(the Scene widget and the line-follower scene) works, and the engine is yours
to fill in.

## Layout

```
lab/
  CMakeLists.txt        the targets below
  sim/                  tracelab_sim      plain C++17, no Qt
    modeldefinition.h     ModelDefinition: a .tlmodel in memory
    odesystem.h           OdeSystem (f and g), compileModel()
    solver.h              SolverKind, SolverSettings, Solver, createSolver()
    sampleclock.h         RunMode, SampleClock, InputPlan, TickResult, Scheduler (ZOH)
    environment.h         Environment, EnvironmentFunction, TrackEnvironment
    modelcompiler.cpp     stub: compileModel()       -> ExprTk goes here
    solver.cpp            stub: createSolver()       -> Euler, RK4, SUNDIALS
    scheduler.cpp         stub: createScheduler()
    environment.cpp       stub: createEnvironment()  -> the .tfg track
  plant/                tracelab_plant
    simulatedplant.h/.cpp   SimulatedPlant: run/pause/reset/step, owns the Scheduler
    plantmanifest.h/.cpp    topic/action names, buildPlantCatalog() (empty body)
    loopbacktransport.h/.cpp  in-process traceview::Transport pair (working)
  control/              tracelab_control
    controlleradapter.h     ControllerAdapter: y[] + reference -> u[] horizon + diag
    controllerhost.h/.cpp   ControllerHost: runs on each loop.state, sends loop.u
    dllcontrolleradapter.h/.cpp     stub: loopctl.h DLL (BTP >= 2.50)
    scriptcontrolleradapter.h/.cpp  stub: QJSEngine controller
  scenes/               tracelab_scenes (working)
    scenerenderer.h         SceneRenderer, SceneInput, SceneParam, SceneFrame, SceneView
    sceneregistry.h/.cpp    SceneRegistry, registerBuiltinScenes()
    scenewidget.h/.cpp      SceneWidget: the "scene" dashboard widget
    sceneconfigeditor.h/.cpp  its properties: type, bindings by name, params
    diffdrivecamera.h/.cpp  the line follower's world -> screen math
    diffdrivescene.h/.cpp   DiffDriveScene, "Line follower"
  module/               tracelab_module
    labmodule.h/.cpp        LabModule: registers the scene widget and the ribbon tab
```

`tools/scene_preview` shows the line-follower scene fed by a synthetic track
and car (`--screenshot <png>` saves one frame). The tests are
`tests/test_sceneregistry.cpp`, `test_diffdrivescene.cpp`,
`test_scenewidget.cpp`, `test_labmodule.cpp` and `test_loopbacktransport.cpp`.

Dependencies only go from `lab/` to `lib/`: `tracelab_plant` uses
`traceview::Transport`, `tracelab_scenes` uses `traceview_dashboard`,
`tracelab_module` uses `traceview_modules`. No `lib/` target links a `lab/`
one. The one change TraceLab needed in `lib/` is generic:
`TelemetryConsumer` ([lib/dashboard/telemetryconsumer.h](../lib/dashboard/telemetryconsumer.h)),
how a widget MainWindow does not know by type gets its telemetry, and the
`"bind"` map in `MainWindow::resolveWidgetBinding()`.

## The contracts

### sim/ (yours)

- **ModelDefinition** is a parsed `.tlmodel` (`"format": "tlmodel/1"`):
  parameters, inputs (unit, min, max; their order is loop.u's), states (unit,
  x0), equations, outputs (their order is loop.state's; an environment
  function can make one several values wide), `"time"`
  continuous or discrete, a scene hint and the environments. Reading the
  JSON is the loader's job, which is free to use any JSON reader.
- **OdeSystem** is the compiled model: `derivatives(t, x, u, dxdt)` (a
  discrete model writes the next state there) and `outputs(t, x, u, y)`, both
  pure, raw arrays in model order (SUNDIALS-friendly), plus live parameters.
  `compileModel()` builds it with ExprTk, kept in `modelcompiler.cpp` alone.
- **Solver** integrates one sample period at a time with the input held:
  `advance(tOut, u, x)`. `SolverSettings` separates the solver step (fixed
  step, or rtol/atol and a max step) from the sample period. `SolverKind`:
  Euler, Rk4, ArkodeExplicit, ArkodeImplicit, CvodeAdams, CvodeBdf.
- **Scheduler** owns the tick: tick k is measured as
  `y_k = g(t_k, x_k, u_{k-1})` and published; `u_k` is then picked from the
  plans received (newest with `refTick <= k`, entry `k - refTick`, last entry
  when shorter, saturated; none: hold, counted late) and held while the
  solver goes to `t_{k+1}`; then the environments' `afterTick()` runs.
  `RunMode` RealTime (with `timeScale`), AsFastAsPossible, Lockstep (tick
  k+1 waits for the plan answering tick k).
- **Environment** exposes native functions to the expressions
  (`track.sensors(x, y, th)`) and advances only in `afterTick()`.
  **TrackEnvironment** fixes what the plant publishes from the track:
  `sim.track` (`{"points": [[x, y], ...], "closed": true}` in metres),
  `sim.truth/next_point` and `coverage` (the pygame reach rule, applied in the
  plant), and `sim.preview` (`tick`, `px[]`, `py[]`: the points ahead in the
  car frame of that tick).

Suggested validation: analytic solutions (first-order step, harmonic
oscillator, each method's convergence order) and regression against the
Python simulator's CSVs (Euler at 80 Hz reproducing `runs/`).

### plant/ and control/ (later phases, T2 and T3)

- `plantmanifest.h` lists every topic and action the plant serves: `loop.state`,
  `loop.output`, `sim.states`, `sim.truth`, `sim.preview`, `sim.params`,
  `sim.track`, and the actions `loop.u` (STREAM), `loop.mode`, `loop.config`,
  `sim.run`/`pause`/`reset`/`step`/`disturb`. `buildPlantCatalog()` fills a
  `btp::Catalog` with the topics. The actions wait for BTP 2.50 (STREAM flag,
  actions in the Catalog, `Node::send_stream()`, the loop profile), which
  exists only locally; TraceView pins 2.48.0.
- `SimulatedPlant` publishes `sim.preview` before `loop.state` of the same
  tick, so the controller host already holds it.
- `ControllerHost` runs the adapter on every `loop.state` (no timer of its
  own), with the `sim.preview` of the same tick as the reference, sends one
  `loop.u` with `ref_tick` and the whole horizon, and publishes `ctl.diag`
  (`ref_tick`, `solve_ms`, `iterations`, `pred_x[]`, `pred_y[]` in the car
  frame of that tick, plus the adapter's own values) and `ctl.info` (the
  adapter's name).
- Showing the plant on the Devices tab needs a device type TraceView can
  create in process; that hook does not exist in `lib/` yet.

### scenes/ (done)

A scene is a `SceneRenderer`: `inputs()` (name, unit, Scalar/Array/Text and
the topic/field "Bind by name" looks for), `params()`, `setParam()`,
`update(SceneFrame)` once per frame with every record since the last one
(the values of one sample stay together) and the newest values,
`paint(QPainter&, SceneView)`, `document()` for Text inputs and `keyAction()`.
A new benchmark is a new class registered in `registerBuiltinScenes()`; the
widget, its editor and the project format do not change.

The Scene widget (`"scene"`) gets its data the way every TraceView widget
does, from `Backend::fieldSample` (scalars, element 0), `arraySample` (whole
arrays) and `textSample` (UTF8 documents), through `TelemetryConsumer`. It
paints at the dashboard render rate (Settings > Dashboard). Its `.tvproj`
config is only `{"scene", "bind", "params"}`; each `"bind"` entry is a normal
name-plus-ids binding with its own `deviceId`.

The line follower reads `sim.truth` (`x`, `y`, `theta`, `tick`,
`next_point`, `coverage`, `t`), `sim.track`, `sim.preview` (`tick`, `px`,
`py`) and `ctl.diag` (`ref_tick`, `pred_x`, `pred_y`, `iterations`,
`solve_ms`) plus `ctl.info`. Car-frame points are taken to the world with
the pose of their own tick (history keyed by tick) and drawn with the current
pose, so everything comes from one pose per frame. P/O/R ask for
`sim.disturb`: `SceneWidget::actionRequested()` is emitted, but nothing sends
it yet, since TraceView has no path for a typed BTP action before BTP 2.50.

The scene shows the plant's own world metres in the coordinates readout
(pygame showed metres + 50 with y down).

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

The plant, the filters and the controller are **blocks of C code** you see,
edit and save in the TraceLab tab, compiled in memory when saved (see "Code
blocks"). The line-follower benchmark runs that way: its car, its qLPV MPC,
GPC and PD are templates you start from. The `.tlmodel` engine of the first
plan (`sim/`) is still a skeleton.

## Building against a local BTP

The plant and the controller host need BTP 2.50 (STREAM actions, action
records in the Catalog, `Node::send_stream()`, `btp/profiles/loop.hpp`),
which exists only in a local checkout of BTP (branch
`feature/stream-actions`); the root `CMakeLists.txt` keeps pinning the
published v2.48.0. Point FetchContent at the checkout (the content name is
`btp`, so the variable is `FETCHCONTENT_SOURCE_DIR_BTP`):

```
cmake -S . -B build-lab -DFETCHCONTENT_SOURCE_DIR_BTP=C:/git/BTP
cmake --build build-lab -j4
```

With the lab ON and an older BTP, configure stops and says so. With
`-DTRACEVIEW_ENABLE_LAB=OFF` the pinned 2.48 is enough.

## Running the line follower in TraceView

1. Start TraceView with `--mode lab` (or the TraceLab launcher, or enable
   TraceLab in Settings > General > Modules).
2. Open the benchmark with **File > Open Example > TraceLab benchmarks >
   Line follower**. Each benchmark is a project of its own,
   `benchmarks/linefollower/linefollower.tvproj` (built in as a resource;
   saving asks for a file):
   the devices "TraceLab: Plant" (the template `linefollower/car` on the
   track environment) and "TraceLab: Controller" (the template
   `linefollower/qlpv_mpc`) and the workspace "Line follower" (the Scene plus
   charts of heading reference vs prediction, lateral error, solve time and
   the wheel speeds the controller uses). The plant starts on its own.
3. The **TraceLab** tab shows the loop as a chain of cards -- plant >
   filters > controller -- with **+ Filter** / **+ Controller** where a block
   can go. Pick a card: the plant shows its clock (Play / Pause / Reset /
   Step, 1x, 2x, as fast as possible, lockstep, the sample rate), its
   settings and its code; a filter or the controller shows its code, where
   it comes from, its parameters (live) and the signals it can read. The
   ribbon has **Add plant**, **Add controller** and **Add scene** (a Scene
   bound by name to both). With the Scene focused, P / O / R change the
   perturbation (`sim.disturb`).

The devices can also be added by hand (Devices tab, **Add Device**). The C++
plant and controller ("TraceLab: Line follower", "TraceLab: Line-follower
controller (C++)") are still registered for older projects and as the
reference the templates are tested against; the tab shows a C++ controller
as a legacy card it can remove.

How the C++ devices are wired (`benchmarks/linefollower/device/`,
`tracelab_lf_devices`) -- `CodePlantDevice` and `LabControllerDevice` (see
"Code blocks") behave the same on the wire:

- `LinePlantDevice` runs `LineFollowerPlant` on a fixed-step clock (80 Hz
  default; real time with a scale, as fast as possible, or lockstep: tick
  k+1 waits up to 10 s for the `loop.u` answering tick k). Its manifest
  (`lfplantmanifest.h`) is the loop profile: `loop.state` (tick, mode, the
  encoders `w_l`/`w_r`, `gyro`, the line array `s[15]`), `loop.output`,
  `sim.truth`, `sim.states`, `sim.preview` (tick, car-frame `px[]`, `py[]`),
  `sim.params`, `sim.track` (UTF8, every k-th track point plus `"stride"` so
  it fits one BTP message, 255 fragments of 210 octets; `next_point` counts
  in those points) and the actions
  `loop.u` (STREAM, ref_tick + `u_l[]`, `u_r[]`, float64), `loop.mode`
  (DANGEROUS, arm), `loop.config`, `sim.run/pause/reset/step/disturb/speed`.
  The text command action (id 1, `Backend::sendCommand()`) takes the same
  names (`sim.disturb 5`). Each link (TraceView, controller) is a
  `BtpProducerPort` (`plant/`): a `btp::Node` with its own session and
  subscriptions behind a `LoopbackTransport`.
- `LineControllerDevice` is a `ControllerHost` (`control/`): a consumer
  `btp::Node` toward the plant (HELLO, manifest, subscriptions to
  `loop.state` and `sim.preview`, `loop.mode` REMOTE + arm, then one
  `send_stream(loop.u)` per tick) and a producer of `ctl.diag` (ref_tick,
  solve_ms, iterations, pred_x/y/theta[], qps, qp_iterations, rho_error,
  theta_ref, theta_pred, w_l_filt, w_r_filt) and `ctl.info` toward TraceView.
- TraceView's side is generic: `TransportType::Module` devices get their
  Transport from the registered type (`lib/core/moduledevicetype.h`).

The standalone `tracelab-linefollower` harness below is unchanged;
`tests/test_lfdevices.cpp` checks that the loop through BTP in lockstep
gives the same steps as that harness.

## Code blocks

A block is one C file (C99 as the Tiny C Compiler takes it) that includes
[`tracelab.h`](code/include/tracelab.h). It **declares** what it reads,
writes and lets you tune with `TL_*` macros -- each one a plain C variable
TraceLab fills before calling the block and reads after -- and defines the
functions of its kind:

| Block | Declares | Defines |
|---|---|---|
| plant | `TL_INPUT`, `TL_STATE`, `TL_OUTPUT[_ARRAY]`, `TL_DISCRETE` | `plant_deriv()` writing `der(x)` (integrated by Euler/RK4 with substeps) or, with `TL_DISCRETE`, `plant_update()` writing `next(x)`; `plant_output()`; `plant_reset()` |
| filter | `TL_IN[_ARRAY]`, `TL_OUT[_ARRAY]`, `TL_INOUT[_ARRAY]` | `filt_step()`, `filt_reset()` |
| controller | `TL_IN[_ARRAY]`, `TL_OUT`, `TL_OUT_PLAN(u, M)`, `TL_PREVIEW(x, y, N)` | `ctl_step()`, `ctl_reset()` |

All take `TL_PARAM` / `TL_PARAM_INT` / `TL_PARAM_ENUM` / `TL_PARAM_BOOL` (shown
next to the code, changed live, kept in the project) and `TL_DIAG` (published
every tick: `ctl.diag` for filters and controllers, `sim.states` for a
plant). Signals are bound **by name**: a controller's `TL_IN(w_l)` reads the
plant's `TL_OUTPUT(w_l)` -- or what a filter's `TL_INOUT(w_l)` made of it --
and its `TL_OUT(u_l)` drives the plant's `TL_INPUT(u_l)`; a misnamed signal
leaves the loop open with the block's card saying what exists. The library
is the libc subset in `tracelab.h` (math, string, malloc, `printf` to the
block's log) plus `tl_ts()`, `tl_tick()`, `tl_noise()`, `tl_disturb()`,
`tl_predicted()` and dense algebra compiled with optimization (`tl_mat_mul`,
`tl_mat_solve`, `tl_qp_box` -- the box QP of `numeric/boxqp.h`).

How it runs (`code/`, `tracelab_code`):

- libtcc (LGPL, built as `tracelab_tcc`, a shared library next to the
  executable) compiles a block in memory in a few milliseconds. Its
  declarations come back from their `tl__decl_*` symbols, in source order.
- `CodeModule::call()` runs a function inside a fault guard: an invalid
  memory access or a division by zero in the block stops it with its line
  ("crash.c:6: invalid memory access") instead of ending TraceView; Reset
  starts it again. tcc's fault handler is patched at configure time
  (`code/CMakeLists.txt`) so faults anywhere else stay the process's own.
- Blocks are files: a template a benchmark ships (read-only) or yours, in
  `Documents/TraceLab/{plants,filters,controllers}`. Saving a template makes
  a copy whose first line is `// tracelab: template <id>` -- what **Back to
  the original** restores. A saved file is compiled again at once (also when
  saved by another editor); a version that does not compile leaves the
  previous one running.
- **Another editor**: "Open in VS Code" / "Open in editor" (the arrow picks
  VS Code, the app for .c files or, on Windows, a program to choose) opens
  the block's file at the cursor's line -- a template is saved as a copy
  first -- and a save there compiles it here. It also writes, in
  `Documents/TraceLab`, `compile_flags.txt` (clangd) and
  `.vscode/c_cpp_properties.json` (VS Code's C/C++ extension; VS Code is
  opened with that folder) pointing at the runtime folder, so the editor
  knows `tracelab.h` and the `TL_*` macros; a file of these without
  TraceLab's marker is the user's and stays.
- **Own window**: the block's editor (code, messages, parameters, signals)
  moves to a window of its own -- another monitor, the scene in sight -- and
  comes back when it closes; **Side panel** hides the parameters and
  signals for a wider editor. Both are remembered (QSettings).
- `LabControllerDevice` (`control/labdevices.h`, type `tracelab.controller`)
  is a `ControllerHost` with the filters (`SignalFilter`,
  `control/signalfilter.h`) and the controller (`CodeControllerAdapter`) in
  C. `CodePlantDevice` (`plant/codeplantdevice.h`, type `tracelab.plant`)
  runs a `CodePlant` on the line follower plant's clock and builds its
  manifest from the code's declarations.
- A plant may live in an **environment** (`plant/plantenvironment.h`):
  native code for its world. The line follower's `linefollower.track`
  (`benchmarks/linefollower/device/trackenvironment.h`) generates the track,
  places the car (states `x`, `y`, `theta`), follows its progress and sends
  the preview and the track's drawing; the car's code queries it through
  `tracelab_track.h` (`tl_track_distance()`, `tl_track_line_half_width()`).

The line follower's templates (`benchmarks/linefollower/code/`) are the C++
benchmark ported: `tests/test_codecontroller.cpp` and `test_codeplant.cpp`
run each one and its C++ original through BTP in lockstep and find **the
same numbers, sample for sample**: the PD, the GPC, the qLPV MPC (taylor and
iterative scheduling) and the car with its sensors (noise off). The qLPV MPC
in C takes about 1 ms a tick (the C++ one 0.3 ms); 12.5 ms are available at
80 Hz.

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
  code/                 tracelab_code (+ tracelab_tcc): blocks of C (see Code blocks)
    include/tracelab.h      what a block sees
    codemodule.h/.cpp       CodeModule: compile, declarations, guarded calls, params
    codehostapi.h/.cpp      the tl_* functions and the libc subset
    coderuntime.h/.cpp      the folder every compile reads (headers, runtime objects)
    codefiles.h/.cpp        CodeLibrary: templates, your files, "back to the original"
    codeblockowner.h/.cpp   CodeBlockOwner: what the tab's block editor works on
  numeric/              tracelab_numeric  the box QP (shared by the C++ MPC and tl_qp_box)
  plant/                tracelab_plant
    labplantdevice.h        LabPlantDevice: a plant as the tab and controllers see it
    codeplant.h/.cpp        CodePlant: a plant in C, ZOH, Euler/RK4
    codeplantdevice.h/.cpp  CodePlantDevice: it as a BTP device, manifest from the code
    plantenvironment.h      PlantEnvironment: a plant's native world
    simulatedplant.h/.cpp   SimulatedPlant: run/pause/reset/step, owns the Scheduler
    plantmanifest.h/.cpp    topic/action names, buildPlantCatalog() (empty body)
    loopbacktransport.h/.cpp  in-process traceview::Transport pair (working)
  control/              tracelab_control
    controlleradapter.h     ControllerAdapter: y[] + reference -> u[] horizon + diag
    controllerhost.h/.cpp   ControllerHost: filters + adapter on each loop.state, sends loop.u
    signalfilter.h          SignalFilter: a stage between loop.state and the controller
    codecontrolleradapter.h/.cpp  a controller in C
    codesignalfilter.h/.cpp       a filter in C
    labdevices.h/.cpp       LabControllerDevice, LabDeviceRegistry, the device types
    dllcontrolleradapter.h/.cpp     stub: loopctl.h DLL (BTP >= 2.50)
    scriptcontrolleradapter.h/.cpp  stub: QJSEngine controller
  sensors/              tracelab_sensors  plain C++17 + Eigen, no Qt (see Sensors)
    sensorrng.h/.cpp        SensorRng (mt19937_64 + own Box-Muller), deriveSeed()
    imperfections.h/.cpp    ImperfectionConfig, ChannelImperfections
    observation.h/.cpp      SensorBlock, LinearObservation, NonlinearObservation, SensorSet
  scenes/               tracelab_scenes (working)
    scenerenderer.h         SceneRenderer, SceneInput, SceneParam, SceneFrame, SceneView
    sceneregistry.h/.cpp    SceneRegistry, registerBuiltinScenes()
    scenewidget.h/.cpp      SceneWidget: the "scene" dashboard widget
    sceneconfigeditor.h/.cpp  its properties: type, bindings by name, params
    diffdrivecamera.h/.cpp  the line follower's world -> screen math
    diffdrivescene.h/.cpp   DiffDriveScene, "Line follower"
  benchmarks/linefollower/  the line-follower benchmark: plant, controllers, runner app (see Benchmarks)
  filters/              the portable C filter runtime and its IIR design (see Filters)
  module/               tracelab_module
    labmodule.h/.cpp        LabModule: registers the scene widget, device types and the tab
    labpanel.h/.cpp         the TraceLab tab: the loop's cards, the picked block's page
    plantpage.h/.cpp        a plant's clock, settings and code
    blockeditor.h/.cpp      a block's code, source, messages, log, parameters, signals
    codeeditor.h/.cpp       the editor: line numbers, C highlighting, error marks
    externaleditor.h/.cpp   opening a block's file in VS Code or another editor
```

`tools/scene_preview` shows the line-follower scene fed by a synthetic track
and car (`--screenshot <png>` saves one frame). The tests are
`tests/test_sceneregistry.cpp`, `test_diffdrivescene.cpp`,
`test_scenewidget.cpp`, `test_labmodule.cpp`, `test_loopbacktransport.cpp` and,
for the line-follower benchmark, `test_lftrackgenerator.cpp`, `test_lfplant.cpp`,
`test_lfsensors.cpp` and `test_lfcontrollers.cpp`; the sensor blocks have
`test_sensors.cpp`.

Dependencies only go from `lab/` to `lib/`: `tracelab_plant` uses
`traceview::Transport`, `tracelab_scenes` uses `traceview_dashboard`,
`tracelab_module` uses `traceview_modules`. No `lib/` target links a `lab/`
one. The one change TraceLab needed in `lib/` is generic:
`TelemetryConsumer` ([lib/dashboard/telemetryconsumer.h](../lib/dashboard/telemetryconsumer.h)),
how a widget MainWindow does not know by type gets its telemetry, and the
`"bind"` map in `MainWindow::resolveWidgetBinding()`.

## Benchmarks

`benchmarks/<name>/` holds one benchmark each: its plant, its controllers and
a runner app, plain C++ below the app so the same code can move behind the
BTP plant and controller host later.

### Line follower (`benchmarks/linefollower/`)

The C++ port of the Python line-follower simulator (`line_follower_simulator`):

```
benchmarks/linefollower/
  plant/          tracelab_lf_plant         plain C++17, no Qt
    pyrandom.h        CPython's random (MT19937, seed, random, uniform, randint), bit for bit
    trackgenerator.h  generate_track(): the same seed and parameters give the
                      same points as Python (splprep/splev periodic spline)
    cardynamics.h     car_dynamics.py: first-order motors, kinematics, the WORLD pose
    trackprogress.h   the pygame Cluster logic in metres: reach rule, coverage,
                      future points in the car frame of the current pose
    linefollowerplant.h  one step_simulation(): motors, pose, reach, StepData,
                      then the sensors (true and measured)
    lfsensors.h       encoders, gyro and the line sensor array (see Sensors)
  controllers/    tracelab_lf_controllers   the qLPV MPC and the GPC (lfcontroller.h)
  app/            tracelab-linefollower     the run window
```

The plant reproduces the Python runs: tests/test_lftrackgenerator.cpp
compares the tracks with Python's point by point (max error below 1e-13 m),
and tests/test_lfplant.cpp replays the saved `runs/compare/*.csv` open loop
(same wheel speeds, pose to 1e-12 m, the same coverage on every step). It
looks for the Python checkout at `TRACELAB_LF_PYTHON_DIR` (default
`../line_follower_simulator`) and skips those cases when it is missing.

Two conventions to know: the world is the pygame track frame (metres, drawn
y down), with the car's heading theta along (cos theta, sin theta), starting
at the first track point with theta = -pi/2; the car frame of the future
points and the predictions is the Python one (x lateral, positive right; y
forward). The reach area is the pygame one: |x| < L_w and -L_w < y < 0 in the
car frame (L_w the wheel distance), visited cell by cell like Track.draw().

`tracelab-linefollower` closes the loop in one process and shows it in the
Line follower scene, fed directly (a provisional harness: no BTP, no
SimulatedPlant or ControllerHost yet). It is built with the lab
(`TRACEVIEW_ENABLE_LAB`); without the controllers library (CMake option
`TRACELAB_LF_CONTROLLERS=OFF` or no `controllers/` folder) it drives u = 0
and says so.

```
cmake -S . -B build-lab
cmake --build build-lab --target tracelab-linefollower
build-lab/lab/tracelab-linefollower/tracelab-linefollower            # qLPV MPC, seed 1112, real time
build-lab/lab/tracelab-linefollower/tracelab-linefollower --controller gpc --fast --log run.csv
build-lab/lab/tracelab-linefollower/tracelab-linefollower --screenshot run.png --screenshot-time 8
```

Options: `--controller qlpv|gpc`, `--seed <n>` (-1 random), `--track-type
lemniscate|circle`, `--fps <hz>` (Ts = 1/fps, default 80), `--horizon <N>`,
`--future-spacing <points>`, `--fast` (as fast as possible) or `--speed <x>`
(time scale), `--max-time <s>`, `--log <file.csv>` (run_logger.py's columns
where they exist, plus `<file>_track.csv`), `--screenshot <png>` with
`--screenshot-time <s>`, `--sensor-noise on|off` (default off),
`--sensor-seed <n>` (default 1, independent of the track seed) and
`--controller-input true|measured` (default measured with noise on, true
otherwise: the controllers keep their interface, with `measured` their wheel
speeds are the encoders'). The scene always shows the true pose; the log
adds `w_l_true`, `w_l_meas`, `w_r_true`, `w_r_meas`, `gyro_true`,
`gyro_meas` and the measured line array `s0`..`s14`. The simulation steps at Ts whatever the repaint rate;
when the controller is slower than Ts the real-time run turns to slow motion.
Keys: P / O perturbation +5 / -5 % (left +q, right -q), R back to 0, Esc
stops; the mouse wheel zooms. At the end (100 % coverage, `--max-time`, Esc or
closing the window) it prints runner.py's metrics: lateral error RMS and max,
mean speed, solve times, QPs. Use a Release build for the qLPV MPC: in Debug,
Eigen makes it several times slower than real time.

## Sensors

`lab/sensors/` (`tracelab_sensors`) separates a plant's TRUE state from what
its sensors MEASURE: `y = h(x, u, env) + imperfections`. Simulation side
only, never exported to firmware.

**Blocks.** `SensorBlock` is the one interface: a name, `outputCount()`
channels, `configure(Ts, masterSeed)`, one `sample(x, u, env)` per tick,
`trueOutput()` (the ideal h) and `measured()`. Blocks of any kind mix in a
`SensorSet`.

- `LinearObservation(name, H, D)`: `y = H x + D u` with the user's Eigen
  matrices (`D` optional).
- `NonlinearObservation(name, m, h)`: `y = h(x, u, env)`, the whole
  measurement vector from one user function -- a `std::function`
  returning an `Eigen::VectorXd`, or a plain function pointer
  `void(const double* x, int nx, const double* u, int nu, const
  SensorEnvironment&, double* y, void* context)` plus its context -- or one
  callable per channel. `SensorEnvironment` carries `t`, `tick` and an
  opaque `user` pointer (a track, a map). Range/bearing, for example:
  `h = [sqrt(x0^2 + x1^2), atan2(x1, x0)]`.
- A model-specific block (an encoder) derives from `SensorBlock`:
  `evaluate()` is h, `transduce()` sits between h and the imperfections.

**Imperfections**, per channel (`ImperfectionConfig`, all off by default),
discrete at Ts and applied in this order: scale factor and constant bias
`(1 + s) h + b`; random-walk bias `b[k+1] = b[k] + sigma_rw sqrt(Ts) n`;
first-order Gauss-Markov `b[k+1] = e^(-Ts/tau) b[k] + sigma sqrt(1 -
e^(-2 Ts/tau)) n` (started stationary); white Gaussian `sigma n`; outliers
(probability, +/- magnitude); saturation `[min, max]`; quantization (LSB);
delay (d samples); own sample rate (every N ticks, held in between);
dropout (probability; hold the last sample or NaN).

**Reproducible noise.** `SensorRng` is `std::mt19937_64` (its sequence is
fixed by the standard) with its own uniform and Box-Muller Gaussian
(`std::normal_distribution` differs between standard libraries). Channel i
of block "name" draws from `deriveSeed(masterSeed, "name/i")` and each
process in it from its own sub-stream, every tick: adding a sensor or
turning one model on never changes another's noise.

**Line follower** (`benchmarks/linefollower/plant/lfsensors.h`), sampled on
every plant step from `x = [w_l, w_r, omega, x, y, theta]`:

| Block | Model | Default |
|---|---|---|
| `encoders` | wheel angle integrated over Ts, counted in whole ticks; speed = delta count * 2 pi / (ticks Ts) | 1024 ticks/rev (0.49 rad/s at 80 Hz), no noise |
| `gyro` | `LinearObservation` picking omega (> 0 turns right) | ideal; noise preset: white 0.02 rad/s, random walk 0.002 rad/s/sqrt(s) |
| `line` | 15 sensors, 0.008 m apart, 0.15 m ahead, s0 leftmost; reading = fraction of a 4 mm footprint disc covered by the line (half-width 0.02 m, the scene's line radius), then a 12-bit ADC (saturation 0..1, LSB 1/4095) | no noise; noise preset: white 0.02 |

The line reading takes the line locally straight: with d the distance to
the track polyline, `[A(w - d) - A(-w - d)] / (pi r^2)`, A the disc's area
on one side of a chord. The nearest segment comes from a uniform grid of the
polyline segments built once, so a query costs a handful of segments and
the lemniscate's crossing shows both branches. `noisySensorParams()` is the
`--sensor-noise on` preset; `PlantParams::sensors.seed` is independent of
the track seed. `LineFollowerPlant::measured()` and `trueSensors()` give
both sides; `StepData` stays the true values.

## Filters

`filters/` is a filter chain written in portable C so the same code runs in
the simulation and, copied as is, in the robot firmware (ESP32, ESP-IDF). It
is split in two:

```
filters/
  runtime/tlfilter.h/.c        tracelab_filters_rt (C99; float) and
                               tracelab_filters_rt_f64 (the same, in double)
  design/tlfilterdesign.h/.cpp tracelab_filters_design (C++17, host only, Iir1)
```

**Runtime** (`tlfilter.h`): C99, no allocation (every state is a struct the
caller owns; the moving average takes a caller buffer), no libc beyond
`math.h`/`string.h`, `tlf_real` = float, or double with `TLF_USE_DOUBLE`
(CMake option `TRACELAB_FILTERS_DOUBLE`). Each block is `tlf_xxx` +
`tlf_xxx_init()` (validates, returns `TLF_OK`/`TLF_EINVAL`), `_step()`,
`_reset()` and `_block()`, the generic `tlf_block` (`step(state, in, out)`,
`reset`, widths `n_in`/`n_out`) that a `tlf_chain` runs in series, with a
vector of up to `TLF_MAX_CHANNELS` values between blocks; a `tlf_bank` runs
one scalar block per channel. Blocks:

| Block | What | State (float, defaults) |
|---|---|---|
| `tlf_movavg` | moving average, circular buffer, exact re-sum once per lap | 16 B + your `len` floats |
| `tlf_median` | median of a window of up to `TLF_MEDIAN_MAX` (31) | 252 B |
| `tlf_lowpass1` | first-order low-pass, alpha = 1 - exp(-ts/tau) (exact step response) | 12 B |
| `tlf_highpass1` | first-order (RC) high-pass, a = exp(-ts/tau) | 16 B |
| `tlf_biquad` | cascade of up to `TLF_BIQUAD_MAX_SECTIONS` (8) sections, Direct Form II transposed: runs every IIR design | 228 B |
| `tlf_complementary` | rate + slow absolute source, optional bias estimate (`ki`) | 24 B |
| `tlf_derivative` | s/(tau s + 1), Tustin | 20 B |
| `tlf_ratelimit`, `tlf_deadzone` | slew limit (rise/fall per second), dead zone (optionally shifted) | 20 B, 8 B |
| `tlf_kf` | linear Kalman filter, `TLF_KF_MAX_N/M/U` (6/4/2) | 692 B |
| `tlf_ekf` | extended Kalman filter, f and h as callbacks returning their Jacobians | ~416 B |
| `tlf_block_make()` | the user block: your step function + your state | yours |

**Design** (`tlfilterdesign.h`, namespace `tlfd`): `design(Spec)` computes the
second-order sections of a Butterworth, Chebyshev I (passband ripple) or
Chebyshev II (stopband attenuation) low-pass, high-pass, band-pass or
band-stop of order 1..16 from the sample rate and the cutoff (or center and
width); `notch(fs, f0, q)` is Iir1's RBJ IIRNotch; `response()` /
`magnitudeDb()` evaluate the cascade; `toRuntime(design, &biquad)` fills the
C struct in the caller's `tlf_real`. The math is
[Iir1](https://github.com/berndporr/iir1) (MIT), fetched by CMake at tag
1.9.5; only its `iir/*.cpp` are compiled, into `tracelab_iir1` (its own
CMakeLists, with a shared library, demos, tests and install rules, is never
added). Iir1 has no Bessel filters (dropped in its 1.x line), so none are
offered. A band filter of order N is N sections, so the default runtime takes
up to order 16 low/high-pass and order 8 band filters.

**Kalman filters and TinyEKF.** The firmware's EKF is
[TinyEKF](https://github.com/AlisonTristao/TinyEKF): a C++ class whose f, h
and Jacobians are generated by SymPy (`optimizer.py`) into the source for one
fixed model. It cannot be called from C and is not generic, so it is not
wrapped; `tlf_kf`/`tlf_ekf` follow its formulation instead -- the same
`predict(u)` / `update(z, u)` calls, P = F P F' + Q, S = H P H' + R,
K = P H' S^-1 by Gauss-Jordan with partial pivoting, a singular S skipping
the measurement -- so moving a filter to the firmware is pasting f and h into
`optimizer.py` (or keeping the C callbacks). One deliberate difference: the
covariance update is Joseph form, (I - K H) P (I - K H)' + K R K', then
symmetrized, where TinyEKF uses P - K H P, which loses symmetry and positive
definiteness in float; worth porting back.

Tests: `tests/test_filters.cpp` (the blocks, in float, plus a check that the
runtime source names no allocator), `test_filters_design.cpp` (magnitude
responses against the closed-form Butterworth/Chebyshev formulas, Butterworth
-3 dB at fc, the double runtime equal to Iir1's own filtering sample by sample
within 1e-11 for all 12 family/response pairs, the notch removing 50 Hz) and
`test_filters_kalman.cpp` (one update against the textbook formulas, the
constant-velocity covariance equal to the steady-state Riccati solution, the
EKF on a pendulum).

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
- The plant and the controller host shown on the Devices tab are the
  line follower's (see "Running the line follower in TraceView"); the
  generic `SimulatedPlant` still waits for the engine.

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
`sim.disturb`: `SceneWidget::actionRequested()` is emitted and the text
command `sim.disturb <value>` goes to the device "x" is bound on.

The scene shows the plant's own world metres in the coordinates readout
(pygame showed metres + 50 with y down).

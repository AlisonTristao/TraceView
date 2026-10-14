# Qt Test suite (see TODO_ARQUITETURA.txt item 7 / CONTRIBUTING.md). Each
# test file is its own executable + CTest case, following the standard
# QTEST_MAIN one-binary-per-test-class pattern -- run all of them with
# `ctest` from the build directory, or a single one directly (e.g.
# `./tests/test_projectstore`).

find_package(Qt6 REQUIRED COMPONENTS Test Network)

function(add_traceview_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_traceview_test(test_projectstore traceview_project)
add_traceview_test(test_workspacemanager traceview_project)
add_traceview_test(test_dashboarditem traceview_dashboard)
add_traceview_test(test_widgetregistry traceview_dashboard)
add_traceview_test(test_dashboardgrid traceview_dashboard)
# The MCP server AI assistants drive TraceView through (lib/agent, see
# docs/MCP.md): argument validation, the JSON-RPC session, and the stdio relay
# talking to a real AgentBridgeServer over a uniquely named local socket.
add_traceview_test(test_agent traceview_agent)
add_traceview_test(test_topicsampledispatcher traceview_dashboard)
# Binding a widget by topic/field NAME (dashboard/bindingresolver.h): names
# re-pointing ids against another device's catalog, ids-only projects gaining
# names, unresolvable names falling back to ids, and the .tvproj round trip.
add_traceview_test(test_bindingresolver traceview_dashboard traceview_project)
# "Add all fields" in the chart editor (series by field name, automatic axes
# per unit, Y range from the declared ranges) and the unit/range a picked
# field prefills in the chart and gauge editors without clobbering the
# user's own values.
add_traceview_test(test_autochartconfig traceview_dashboard)
# Devices panel data model/card/grid, plus its own JSON persistence (see
# lib/devices). Only traceview_devices; no dashboard/protocol dependency.
add_traceview_test(test_device traceview_devices)
add_traceview_test(test_deviceconfigdialog traceview_devices)
add_traceview_test(test_devicesgrid traceview_devices)
# hub.peers reassembly -- six parallel variable-length arrays arriving one
# (field, element) at a time. Only traceview_devices: the accumulator lives
# below the protocol layer on purpose (see lib/devices/hubpeeraccumulator.h),
# so this exercises the decode with no BTP and no QWidget involved.
add_traceview_test(test_hubpeeraccumulator traceview_devices)
# The combine rule for a hub child's green/amber dot -- end-to-end robot frame
# vs the dongle's hub.peers fallback, the offline debounce, and the sticky
# "known" flag. Pure function (device.h), no MainWindow, no QWidget.
add_traceview_test(test_hubchildpresence traceview_devices)
add_traceview_test(test_chartdata traceview_dashboard)
add_traceview_test(test_multisourcechart traceview_dashboard)
# Round-number axis ticks and the chart style sheet (widgets/chartscale.h,
# widgets/chartstyle.h) -- pure functions, no painting.
add_traceview_test(test_chartscale traceview_dashboard)
add_traceview_test(test_chartstyle traceview_dashboard)
# Frame styles (card/control shapes, include/traceview/framestyle.h).
add_traceview_test(test_framestyle traceview_theme)
# Decimal fields: both separators, adaptive arrows, commit on Enter/focus out
# (theme/doublespinbox.h).
add_traceview_test(test_doublespinbox traceview_theme)
# Appearance tokens and the appearance catalog/presets (core/appearancecatalog.h).
add_traceview_test(test_appearance traceview_ui)
add_traceview_test(test_controldata traceview_dashboard)
# topico 19: the dumb-terminal line model (\r/\b/\n handling) and keystroke
# forwarding are exercised directly with QTest -- CONTRIBUTING.md's "nothing
# UI-chrome-level" gap did not previously include any single-widget test,
# but the existing precedent (test_dashboardgrid uses QTest::mousePress on
# real QWidgets) already covers widget-level QTest usage in this repo.
add_traceview_test(test_serialterminalwidget traceview_dashboard)
# The multi-tab serial monitor: config migration (bare deviceId / {} -> one
# tab), the tab list, device-name labels, per-device inbound routing and the
# device-tagged outbound signal -- all widget-level, no transport.
add_traceview_test(test_serialmonitorwidget traceview_dashboard)
add_traceview_test(test_textboardwidget traceview_dashboard)
# The Audio Spectrum widget's signal side: FFT scaling, the jitter buffer's
# target/underrun/overflow rules and resampling (widgets/audiostream.h), and
# the widget decoding a stream topic -- with playback off, no sound card.
add_traceview_test(test_audiostream traceview_dashboard)
# The live robot-log table model: field mapping per column, severity
# foreground coloring, and the bounded ring (oldest dropped past kCapacity) --
# see lib/dashboard/widgets/robotlogmodel.h.
add_traceview_test(test_robotlogmodel traceview_dashboard)
# The multi-tab robot log widget: config migration (bare deviceId / {} -> one
# tab), the tab list, device-name labels, per-device inbound routing and
# per-tab Clear -- same coverage shape as test_serialmonitorwidget.
add_traceview_test(test_robotlogwidget traceview_dashboard)
# topico 26: a Device that connects THROUGH another Device. traceview_ui so
# it can build a real DeviceConnection/BtpBackend pair, which is what the
# multiplexing test drives -- no port and no hardware are involved.
add_traceview_test(test_hubtransport traceview_ui traceview_protocol btp::codec)
# The three-implementations-one-contract check: this derives the same key
# octets as bally_OS's provisioner and the hub's console command, against a
# vector that provisioner actually produced.
add_traceview_test(test_keyderivation traceview_protocol)
# The dongle clock round trip: which COMMAND_RESULT counts as this client's
# reply, and when a drift is worth correcting. Real BtpSession/ProtocolRouter,
# no transport -- what the session would have written is decoded straight out
# of bytesToWrite(), the same way test_hubendpoint does it.
add_traceview_test(test_clocksync traceview_protocol btp::codec)
# The hub binding: what the desktop tells the dongle about which robot each
# child device speaks to. Same no-transport fixture as test_clocksync. Worth
# its own suite because every property here fails SILENTLY when it regresses
# -- an unbound child does not error, it gets its commands executed on the
# dongle instead of on its robot.
add_traceview_test(test_hubbinder traceview_protocol btp::codec)
# MANIFEST_REQUEST/MANIFEST_DATA: when a request is worth sending, and a
# bounds-checked walk over the response. Same no-transport fixture as
# test_clocksync -- requests are decoded out of bytesToWrite(), responses
# are fed in as hand-built payloads.
add_traceview_test(test_manifestclient traceview_protocol btp::codec)
# topico 28, desktop side: a child device as an ENDPOINT -- it originates its
# own traffic and every piece of it has to be addressed to its own robot.
add_traceview_test(test_hubendpoint traceview_ui traceview_protocol btp::codec)
# bally_channels.h is one of three byte-identical copies (see the test's own
# header comment). The absolute path is passed in rather than resolved at run
# time so the check cannot pass by silently finding nothing, and so it works
# from whatever directory ctest happens to run in.
add_traceview_test(test_ballychannels traceview_protocol btp::codec)
target_compile_definitions(test_ballychannels PRIVATE
    BALLY_CHANNELS_HEADER_PATH="${CMAKE_SOURCE_DIR}/include/bally_channels.h")
add_traceview_test(test_serialmanager traceview_ui)
# TCP transport -- an asynchronous local-server loopback covers connection,
# bidirectional bytes and clean close without requiring robot hardware.
add_traceview_test(test_tcptransport traceview_ui Qt6::Network)
# USB HID transport (BTP's "usb_hid" profile) -- mirrors
# test_serialmanager's shape/coverage (mostly the "can't reach real
# hardware in CI" paths: starts disconnected, closes/writes cleanly with
# nothing open, invalid target fails without crashing).
add_traceview_test(test_usbhidmanager traceview_ui)
# One device's real connection (Transport + Backend pair, ambient reconnect
# intent) -- see lib/core/deviceconnection.h. traceview_ui so it can
# construct a BtpBackend the same way DeviceConnection itself does.
add_traceview_test(test_deviceconnection traceview_ui Qt6::Network)
# In-process device types a module registers (core/moduledevicetype.h): the
# registry, DeviceConnection dialing the factory's transport, and the
# TransportType::Module device's .tvproj round trip.
add_traceview_test(test_moduledevicetype traceview_transport traceview_devices)
# Developer/User access mode (lib/core/usermodemanager.h): default "admin"
# account seeding on first run, login/logout, and the account-management
# mutators' Developer-mode gating (refusing to touch accounts, or to drop
# the last remaining one, outside it). traceview_ui since UserModeManager
# lives under lib/core. Isolates every QSettings read/write to a throwaway
# directory (see the test's own initTestCase()) so it neither reads nor
# leaves behind the real TraceView account list.
add_traceview_test(test_usermodemanager traceview_ui)
add_traceview_test(test_mainwindowchrome traceview_ui)
# Optional application modules (lib/modules, docs/MODULES.md): the registry,
# saved enabled state and --mode on their own, then MainWindow installing the
# enabled ones and Settings listing them.
add_traceview_test(test_moduleregistry traceview_modules traceview_dashboard)
add_traceview_test(test_mainwindowmodules traceview_ui traceview_modules traceview_dashboard)

if(TRACEVIEW_ENABLE_LAB)
    # TraceLab (lab/): the scene registry; the line-follower camera (a point
    # under the car maps to the car's anchor, forward motion moves the world
    # down, a right turn rotates it counter-clockwise, the pygame step rule)
    # and the alignment of car-frame points through the pose of their tick;
    # the Scene widget's config (editor and .tvproj round trip, bind by name,
    # sample routing); LabModule's install; the loopback transport pair.
    add_traceview_test(test_sceneregistry tracelab_scenes)
    add_traceview_test(test_diffdrivescene tracelab_scenes)
    # The Furuta pendulum scene: inputs bound by name, angles and trail,
    # the mass where the angles put it, the disturbance keys, a painted frame
    # (saved to $TRACELAB_FURUTA_SCENE_PNG when set).
    add_traceview_test(test_furutascene tracelab_scenes)
    add_traceview_test(test_scenewidget tracelab_scenes traceview_project)
    add_traceview_test(test_labmodule tracelab_module tracelab_scenes traceview_dashboard
                       traceview_transport traceview_agent)
    tracelab_copy_tcc_runtime(test_labmodule)
    # The TraceLab tab on a live line follower: the loop as plant > filters >
    # controller cards, blocks added from the templates, a picked block's
    # code/parameters/signals, Save making an edited template a file of the
    # user's, back to the original, the configuration reaching the project.
    add_traceview_test(test_labpanel tracelab_module tracelab_control tracelab_lf_devices
                       traceview_dashboard traceview_modules)
    tracelab_copy_tcc_runtime(test_labpanel)
    add_traceview_test(test_loopbacktransport tracelab_plant)
    # The line-follower benchmark's plant (lab/benchmarks/linefollower/plant):
    # the track generator against the Python simulator (CPython's random bit
    # for bit, the tracks of tests/data/linefollower point by point, and the
    # saved runs/*_track.csv when line_follower_simulator is checked out at
    # TRACELAB_LF_PYTHON_DIR); the motors' discrete equations, the world pose
    # against the pygame track transform, the reach rule and the car-frame
    # future points, and an open-loop replay of the saved Python runs.
    set(TRACELAB_LF_PYTHON_DIR "${CMAKE_SOURCE_DIR}/../line_follower_simulator" CACHE PATH
        "line_follower_simulator checkout used by the line-follower tests (skipped when missing)")
    add_traceview_test(test_lftrackgenerator tracelab_lf_plant)
    add_traceview_test(test_lfplant tracelab_lf_plant)
    foreach(test test_lftrackgenerator test_lfplant)
        target_compile_definitions(${test} PRIVATE
            TRACELAB_LF_TESTDATA="${CMAKE_CURRENT_SOURCE_DIR}/data/linefollower"
            TRACELAB_LF_PYTHON_DIR="${TRACELAB_LF_PYTHON_DIR}")
    endforeach()
    # Sensor emulation (lab/sensors): reproducible noise streams (the standard
    # mt19937_64, own Box-Muller, one stream per sensor), the statistics of
    # white noise, random walk and Gauss-Markov, quantization, saturation,
    # delay, own rate, dropout and outliers, and the linear and nonlinear
    # observation blocks; then the line follower's encoders, line sensor
    # array and the plant's true/measured outputs.
    add_traceview_test(test_sensors tracelab_sensors)
    add_traceview_test(test_lfsensors tracelab_lf_plant)
    # Code blocks (lab/code): a C file compiled in memory by libtcc, its TL_*
    # declarations in source order, parameters kept across recompiles, the
    # tl_* functions and printf, compiler errors and runtime faults with their
    # line (the process's own exceptions unaffected), the templates library.
    add_traceview_test(test_codemodule tracelab_code)
    tracelab_copy_tcc_runtime(test_codemodule)
endif()

if(TRACEVIEW_ENABLE_LAB AND TARGET tracelab_filters_rt)
    # TraceLab's filters (lab/filters, lab/README.md "Filters"). test_filters:
    # the C runtime's blocks in float, as the firmware builds them (moving
    # average, median, first-order low/high-pass, biquad, complementary,
    # derivative, rate limiter, dead zone, chain/bank, user block, the
    # no-allocation contract on the source file itself). test_filters_design:
    # the Iir1-based designs against the closed-form magnitude responses, the
    # double runtime reproducing Iir1's filtering sample by sample, the notch.
    # test_filters_kalman: KF/EKF in float against the textbook update and the
    # steady-state Riccati solution, the EKF on a pendulum.
    add_traceview_test(test_filters tracelab_filters_rt)
    target_compile_definitions(test_filters PRIVATE
        TLF_RUNTIME_SOURCE="${CMAKE_SOURCE_DIR}/lab/filters/runtime/tlfilter.c")
    add_traceview_test(test_filters_design tracelab_filters_design tracelab_filters_rt_f64 tracelab_iir1)
    add_traceview_test(test_filters_kalman tracelab_filters_rt)
endif()

if(TARGET tracelab_lf_controllers)
    # The line-follower controllers (lab/benchmarks/linefollower/controllers),
    # the C++ port of line_follower_simulator's mpc/: the model against the
    # kinematics it embeds, the box QP against its KKT conditions and its
    # fallback, the turn-sign convention, input bounds, determinism, every
    # scheduling strategy x embedding, and GPC with both heading references.
    # Plain C++ under test, Qt Test only as the harness.
    add_traceview_test(test_lfcontrollers tracelab_lf_controllers)
endif()

if(TARGET tracelab_lf_devices)
    # The line follower as TraceView devices (lab/benchmarks/linefollower/
    # device): the plant's manifest, the plant over the loopback to a real
    # BtpBackend (TransportType::Module), a lockstep closed loop through BTP
    # equal to the in-process harness, and sim.disturb by command.
    add_traceview_test(test_lfdevices tracelab_lf_devices tracelab_lf_controllers
                       traceview_transport traceview_devices traceview_backend)
    tracelab_copy_tcc_runtime(test_lfdevices)
    # Controllers and filters in C (lab/control: CodeControllerAdapter,
    # CodeSignalFilter, LabControllerDevice) on the line follower through BTP:
    # the PD template equal to the C++ PD step for step, filters replacing and
    # adding signals by name, misnamed signals reported, faults holding the
    # input until reset, a saved file compiled again while running.
    add_traceview_test(test_codecontroller tracelab_lf_devices tracelab_control)
    tracelab_copy_tcc_runtime(test_codecontroller)
    # Plants in C (lab/plant: CodePlant, CodePlantDevice) and the track
    # environment: RK4 against the exact solution, a generic loop bound by
    # the code's names (rebinding after a new version), the line follower's
    # car in C equal to the C++ plant sample for sample, and the benchmark
    # project's devices (noise, Kalman) on the line.
    add_traceview_test(test_codeplant tracelab_lf_devices tracelab_control tracelab_plant)
    tracelab_copy_tcc_runtime(test_codeplant)
    target_compile_definitions(test_codeplant PRIVATE
        TRACELAB_LF_PROJECT="${CMAKE_SOURCE_DIR}/lab/benchmarks/linefollower/linefollower.tvproj")
    # The Furuta pendulum benchmark (lab/benchmarks/furuta): the plant in C
    # conserving energy, the qLPV NMPC (warm-start, iterative) and the energy
    # swing-up + LQR swinging it up, balancing it and following the arm's
    # step, frozen and Taylor within bounds, closed directly as the devices
    # step them.
    add_traceview_test(test_furuta tracelab_furuta tracelab_control tracelab_plant)
    tracelab_copy_tcc_runtime(test_furuta)
endif()

# Generic telemetry value types (lib/telemetry) -- no BTP dependency at all
# (traceview_telemetry links just Qt6::Core; see lib/CMakeLists.txt).
add_traceview_test(test_telemetryseriesbuffer traceview_telemetry)
add_traceview_test(test_telemetryhistory traceview_telemetry)

# BTP v1 client (topico 14) -- protocol/transport layer only, no QWidget
# dependency (traceview_protocol links just Qt6::Core and btp::codec; see
# lib/CMakeLists.txt). Satisfies topico 14's CRITERIO DE ACEITE "parser/
# protocolo possui testes sem QWidget".
add_traceview_test(test_telemetrycatalog traceview_protocol)
add_traceview_test(test_protocolrouter traceview_protocol)
add_traceview_test(test_btpsession traceview_protocol)
# topico 25: BtpSession's framing axis vs. its encode-profile axis, now that
# they are separate fields -- the EspNow-under-pre-framed combination, the
# never-re-encode contract of sendRawFrame(), and frameBytesReceived()'s
# per-fragment whole-frame octets. Kept in its own file so test_btpsession
# (the pre-existing behavior of the two real transports) stays untouched by
# that refactor.
add_traceview_test(test_btpsessionframing traceview_protocol)
add_traceview_test(test_btphandshake traceview_protocol)
# Graceful teardown of an established console session: exact
# CONTROL/SESSION_CLOSE wire payload, one-shot behavior, and producer
# sequence continuity with TERMINAL_IN.
add_traceview_test(test_sessionclose traceview_protocol btp::codec)
add_traceview_test(test_telemetryfieldrouter traceview_protocol)
# topico 17 (assinaturas e controle de taxa): the STATUS v1/v2 payload parser
# and the reference-counted SUBSCRIBE/UNSUBSCRIBE aggregation, both pure
# protocol-layer classes -- no QWidget involved, the subscribers are opaque
# handles.
add_traceview_test(test_statusreport traceview_protocol)
add_traceview_test(test_subscriptionmanager traceview_protocol)
# Reads a .blog file (headerless, concatenated BTP Log frames -- see
# lib/protocol/logfilereader.h) -- protocol-layer only, no QWidget.
add_traceview_test(test_logfilereader traceview_protocol)
# mDNS (RFC 6762) query/response wire format (lib/ota/mdnsresolver.*) --
# pure byte-level encode/decode, no socket I/O involved (that part can only
# really be checked against a real mDNS responder on the network).
# OTA status polling against bally_OS's HTTP side channel. No server is
# involved: the requests go to TEST-NET-1 (RFC 5737), which is guaranteed
# unroutable, so what is exercised is this client's own bookkeeping --
# in-flight tracking and the per-device coalescing that keeps a 1 Hz poll
# from cancelling a request allowed 4 seconds to answer.
add_traceview_test(test_otaclient traceview_ota)
add_traceview_test(test_mdnsresolver traceview_ota)

# Self-update (lib/updater). Both pure logic, no live network round trip
# (see test_updatechecker.cpp's own comment, same reasoning as test_otaclient
# above): tag/version precedence and the GitHub /releases response parsing
# UpdateChecker's network callback hands off to.
add_traceview_test(test_semver traceview_updater)
add_traceview_test(test_updatechecker traceview_updater)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT TRACEVIEW_FLATPAK_BUILD)
    add_traceview_test(test_updateinstaller traceview_updater)
endif()
add_traceview_test(test_updatedownloader traceview_updater)

# Compile the Flatpak policy separately so ordinary Windows/Linux CI also
# exercises the managed-update guards without needing a Flatpak runtime.
add_traceview_test(test_flatpak_updater Qt6::Network)
target_sources(test_flatpak_updater PRIVATE
    ../lib/updater/updatechecker.cpp
    ../lib/updater/updatedownloader.cpp
    ../lib/updater/updateinstaller.cpp
    ../lib/updater/semver.cpp)
target_include_directories(test_flatpak_updater PRIVATE ../lib ../include ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(test_flatpak_updater PRIVATE TRACEVIEW_FLATPAK_BUILD)

# Diagnostics (lib/diagnostics): the two in-memory buffers behind the
# Notification History window and the BTP Traffic Monitor, the hex-dump
# formatter they share, and the seal/open round trip the monitor's Decrypt
# box performs (ChannelSeal, the same path BtpBackend uses in production).
add_traceview_test(test_notificationlog traceview_diagnostics)
add_traceview_test(test_framelog traceview_diagnostics)
add_traceview_test(test_hexdump traceview_diagnostics)
add_traceview_test(test_channelopen traceview_protocol btp::codec)
# TAREFAS_TCP_BLE_ANDROID.txt T21-T24: BtpBackend::setDirectEndpointKey() --
# a direct TCP (later BLE) session's counterpart to setHubEndpoint()
# (test_hubendpoint), narrow on purpose: only wires the channel-B key so a
# sealed reply from the robot (TERMINAL_OUT/TELEMETRY/MANIFEST_DATA/
# SUBSCRIBE_RESULT/UNSUBSCRIBE_RESULT/COMMAND_RESULT) can be opened, without
# adopting any of the hub-child role's other side effects.
add_traceview_test(test_directendpointkey traceview_protocol btp::codec)

if(TRACEVIEW_ENABLE_BLE)
    # T27/T28-T30: BleDiscoveryService's start/stop lifecycle and
    # BleTransport's own-input validation (empty address, close()/write()
    # with nothing open). Deliberately does NOT exercise a real scan or GATT
    # round trip -- that needs either real hardware (T37) or the simulated
    # peripheral layer T36 is for, neither of which exists yet. What's
    # covered here is exactly what test_tcptransport's own "can't reach real
    # hardware in CI" paths cover for TCP: the object doesn't crash or hang
    # with no adapter/peripheral behind it.
    add_traceview_test(test_bletransport traceview_ui Qt6::Bluetooth)
endif()

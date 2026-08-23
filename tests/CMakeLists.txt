# Qt Test suite (see TODO_ARQUITETURA.txt item 7 / CONTRIBUTING.md). Each
# test file is its own executable + CTest case, following the standard
# QTEST_MAIN one-binary-per-test-class pattern -- run all of them with
# `ctest` from the build directory, or a single one directly (e.g.
# `./tests/test_projectstore`).

find_package(Qt6 REQUIRED COMPONENTS Test)

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
# Devices panel data model/card/grid, plus its own JSON persistence (see
# lib/devices). Only traceview_devices; no dashboard/protocol dependency.
add_traceview_test(test_device traceview_devices)
add_traceview_test(test_devicesgrid traceview_devices)
add_traceview_test(test_chartdata traceview_dashboard)
add_traceview_test(test_controldata traceview_dashboard)
# topico 19: the dumb-terminal line model (\r/\b/\n handling) and keystroke
# forwarding are exercised directly with QTest -- CONTRIBUTING.md's "nothing
# UI-chrome-level" gap did not previously include any single-widget test,
# but the existing precedent (test_dashboardgrid uses QTest::mousePress on
# real QWidgets) already covers widget-level QTest usage in this repo.
add_traceview_test(test_serialterminalwidget traceview_dashboard)
# topico 26: a Device that connects THROUGH another Device. traceview_ui so
# it can build a real DeviceConnection/BtpBackend pair, which is what the
# multiplexing test drives -- no port and no hardware are involved.
add_traceview_test(test_hubtransport traceview_ui traceview_protocol btp::codec)
# The three-implementations-one-contract check: this derives the same key
# octets as bally_OS's provisioner and the hub's console command, against a
# vector that provisioner actually produced.
add_traceview_test(test_keyderivation traceview_protocol)
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
# USB HID transport (BTP's "usb_hid" profile) -- mirrors
# test_serialmanager's shape/coverage (mostly the "can't reach real
# hardware in CI" paths: starts disconnected, closes/writes cleanly with
# nothing open, invalid target fails without crashing).
add_traceview_test(test_usbhidmanager traceview_ui)
# One device's real connection (Transport + Backend pair, ambient reconnect
# intent) -- see lib/core/deviceconnection.h. traceview_ui so it can
# construct a BtpBackend the same way DeviceConnection itself does.
add_traceview_test(test_deviceconnection traceview_ui)

# Generic telemetry value types (lib/telemetry) -- no BTP dependency at all
# (traceview_telemetry links just Qt6::Core; see lib/CMakeLists.txt).
add_traceview_test(test_telemetryseriesbuffer traceview_telemetry)

# BTP v1 client (topico 14) -- protocol/transport layer only, no QWidget
# dependency (traceview_protocol links just Qt6::Core and btp::codec; see
# lib/CMakeLists.txt). Satisfies topico 14's CRITERIO DE ACEITE "parser/
# protocolo possui testes sem QWidget".
add_traceview_test(test_telemetrycatalog traceview_protocol)
add_traceview_test(test_packedledecoder traceview_protocol)
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

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
add_traceview_test(test_chartdata traceview_dashboard)
add_traceview_test(test_controldata traceview_dashboard)
# topico 19: the dumb-terminal line model (\r/\b/\n handling) and keystroke
# forwarding are exercised directly with QTest -- CONTRIBUTING.md's "nothing
# UI-chrome-level" gap did not previously include any single-widget test,
# but the existing precedent (test_dashboardgrid uses QTest::mousePress on
# real QWidgets) already covers widget-level QTest usage in this repo.
add_traceview_test(test_serialterminalwidget traceview_dashboard)
add_traceview_test(test_serialmanager traceview_ui)

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
add_traceview_test(test_btphandshake traceview_protocol)
add_traceview_test(test_telemetryfieldrouter traceview_protocol)
# topico 17 (assinaturas e controle de taxa): the STATUS v1/v2 payload parser
# and the reference-counted SUBSCRIBE/UNSUBSCRIBE aggregation, both pure
# protocol-layer classes -- no QWidget involved, the subscribers are opaque
# handles.
add_traceview_test(test_statusreport traceview_protocol)
add_traceview_test(test_subscriptionmanager traceview_protocol)

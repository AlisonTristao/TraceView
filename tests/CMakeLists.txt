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
add_traceview_test(test_dashboarditem traceview_dashboard)
add_traceview_test(test_widgetregistry traceview_dashboard)
add_traceview_test(test_dashboardgrid traceview_dashboard)
add_traceview_test(test_chartdata traceview_dashboard)
add_traceview_test(test_serialmanager traceview_ui)
add_traceview_test(test_serialprotocol traceview_ui)
add_traceview_test(test_serialdatarouter traceview_ui)

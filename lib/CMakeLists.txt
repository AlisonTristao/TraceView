configure_file(
    ${CMAKE_SOURCE_DIR}/include/traceview/version.h.in
    ${CMAKE_BINARY_DIR}/generated/traceview/version.h
    @ONLY
)

add_library(traceview_core STATIC
    core/mainwindow.cpp
    core/mainwindow.h
)

target_include_directories(traceview_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(traceview_core PUBLIC
    Qt6::Widgets
    Qt6::SerialPort
)

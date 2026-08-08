configure_file(
    ${CMAKE_SOURCE_DIR}/include/traceview/version.h.in
    ${CMAKE_BINARY_DIR}/generated/traceview/version.h
    @ONLY
)

add_library(traceview_core STATIC
    core/aboutdialog.cpp
    core/aboutdialog.h
    core/mainwindow.cpp
    core/mainwindow.h
    core/ribbon.cpp
    core/ribbon.h
    dashboard/dashboarditem.cpp
    dashboard/dashboarditem.h
    dashboard/dashboardwidget.h
    dashboard/dummywidgets.cpp
    dashboard/dummywidgets.h
    dashboard/widgetregistry.cpp
    dashboard/widgetregistry.h
    dashboard/dashboardcell.cpp
    dashboard/dashboardcell.h
    dashboard/dashboardcommands.cpp
    dashboard/dashboardcommands.h
    dashboard/dashboardgrid.cpp
    dashboard/dashboardgrid.h
    project/projectstore.cpp
    project/projectstore.h
    theme/palettes.cpp
    theme/palettes.h
    theme/stylesheet.cpp
    theme/stylesheet.h
    theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/include/traceview/thememanager.h
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

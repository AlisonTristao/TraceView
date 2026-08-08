configure_file(
    ${CMAKE_SOURCE_DIR}/include/traceview/version.h.in
    ${CMAKE_BINARY_DIR}/generated/traceview/version.h
    @ONLY
)

# traceview_theme: palette/stylesheet definitions and the ThemeManager
# singleton (public header lives in include/traceview so the app can reach
# it without depending on any other layer).
add_library(traceview_theme STATIC
    theme/palettes.cpp
    theme/palettes.h
    theme/stylesheet.cpp
    theme/stylesheet.h
    theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/include/traceview/thememanager.h
)

target_include_directories(traceview_theme PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_theme PUBLIC
    Qt6::Widgets
)

# traceview_project: .tvproj (de)serialization. No dependency on the UI or
# dashboard layers.
add_library(traceview_project STATIC
    project/projectstore.cpp
    project/projectstore.h
)

target_include_directories(traceview_project PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_project PUBLIC
    Qt6::Widgets
)

# traceview_dashboard: the editable widget grid (items, cells, undo
# commands, widget registry). Depends on traceview_theme for painting.
add_library(traceview_dashboard STATIC
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
)

target_include_directories(traceview_dashboard PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_dashboard
    PUBLIC
        Qt6::Widgets
    PRIVATE
        traceview_theme
)

# traceview_ui: MainWindow/Ribbon/AboutDialog — application chrome and
# coordination. Depends on the dashboard/project/theme layers to wire them
# together; nothing else is allowed to depend on traceview_ui.
add_library(traceview_ui STATIC
    core/aboutdialog.cpp
    core/aboutdialog.h
    core/mainwindow.cpp
    core/mainwindow.h
    core/ribbon.cpp
    core/ribbon.h
    core/ribbonicons.cpp
    core/ribbonicons.h
)

target_include_directories(traceview_ui PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(traceview_ui
    PUBLIC
        Qt6::Widgets
        Qt6::SerialPort
    PRIVATE
        traceview_dashboard
        traceview_project
        traceview_theme
)

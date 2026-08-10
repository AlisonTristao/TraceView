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

# traceview_protocol: the BTP v1 client -- transport-independent framing
# (BtpSession), MessageType dispatch (ProtocolRouter), the telemetry data
# model (TelemetrySample, TelemetryCatalog, decodePackedLe,
# TelemetryFieldRouter, TelemetrySeriesBuffer). Only QtCore: no QWidget, no
# QSerialPort -- see CONTRIBUTING.md's transport/protocol/presentation split
# and tests/test_btpsession.cpp and friends (topico 14 CRITERIO DE ACEITE
# "parser/protocolo possui testes sem QWidget").
add_library(traceview_protocol STATIC
    protocol/btpframe.cpp
    protocol/btpframe.h
    protocol/btphandshake.cpp
    protocol/btphandshake.h
    protocol/btpsession.cpp
    protocol/btpsession.h
    protocol/packedledecoder.cpp
    protocol/packedledecoder.h
    protocol/protocolrouter.cpp
    protocol/protocolrouter.h
    protocol/telemetrycatalog.cpp
    protocol/telemetrycatalog.h
    protocol/telemetryfieldrouter.cpp
    protocol/telemetryfieldrouter.h
    protocol/telemetryseriesbuffer.cpp
    protocol/telemetryseriesbuffer.h
    protocol/telemetrysample.h
)

target_include_directories(traceview_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_protocol PUBLIC
    Qt6::Core
    btp::codec
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
    dashboard/widgetconfigeditor.h
    dashboard/widgets/chartconfigeditor.cpp
    dashboard/widgets/chartconfigeditor.h
    dashboard/widgets/chartdata.cpp
    dashboard/widgets/chartdata.h
    dashboard/widgets/chartwidgets.cpp
    dashboard/widgets/chartwidgets.h
    dashboard/widgets/gaugeconfigeditor.cpp
    dashboard/widgets/gaugeconfigeditor.h
    dashboard/widgets/serialmonitorwidget.cpp
    dashboard/widgets/serialmonitorwidget.h
    dashboard/widgets/serialterminalwidget.cpp
    dashboard/widgets/serialterminalwidget.h
    dashboard/widgets/controldata.cpp
    dashboard/widgets/controldata.h
    dashboard/widgets/controlwidgets.cpp
    dashboard/widgets/controlwidgets.h
    dashboard/widgets/controlconfigeditor.cpp
    dashboard/widgets/controlconfigeditor.h
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
        traceview_protocol
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
    core/propertiespanel.cpp
    core/propertiespanel.h
    core/ribbon.cpp
    core/ribbon.h
    core/ribbonicons.cpp
    core/ribbonicons.h
    core/ribbontabbar.cpp
    core/ribbontabbar.h
    core/serialmanager.cpp
    core/serialmanager.h
    core/serialwidgetbridge.cpp
    core/serialwidgetbridge.h
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
        traceview_protocol
        traceview_theme
)

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

# traceview_telemetry: generic telemetry value types and data structures
# with no BTP dependency -- TelemetryFieldBinding, TopicSubscriptionState,
# StatusTopicRecord, TelemetrySeriesBuffer. These describe what a Backend
# exposes to the UI (see lib/backend/backend.h), not how BTP encodes
# anything on the wire, so they live below traceview_protocol instead of
# inside it -- a from-scratch Backend implementation (or the dashboard
# widgets themselves) can use them without pulling in btp::codec. Only
# QtCore.
add_library(traceview_telemetry STATIC
    telemetry/telemetrybinding.h
    telemetry/subscriptionstate.h
    telemetry/statustopicrecord.h
    telemetry/telemetryseriesbuffer.cpp
    telemetry/telemetryseriesbuffer.h
)

target_include_directories(traceview_telemetry PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_telemetry PUBLIC
    Qt6::Core
)

# traceview_backend: the Backend interface itself (lib/backend/backend.h) --
# what MainWindow/SerialWidgetBridge depend on instead of any one protocol's
# concrete classes. Depends only on traceview_telemetry, not btp::codec: a
# third party implementing a different protocol depends on this target and
# traceview_telemetry, never on traceview_protocol/BTP.
add_library(traceview_backend STATIC
    backend/backend.h
)

set_target_properties(traceview_backend PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(traceview_backend PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_backend PUBLIC
    Qt6::Core
    traceview_telemetry
)

# traceview_protocol: the BTP v1 client -- transport-independent framing
# (BtpSession), MessageType dispatch (ProtocolRouter), the telemetry data
# model (TelemetrySample, TelemetryCatalog, decodePackedLe,
# TelemetryFieldRouter) and BtpBackend, the Backend implementation backed by
# this stack. Only QtCore: no QWidget, no QSerialPort -- see CONTRIBUTING.md's
# transport/protocol/presentation split and tests/test_btpsession.cpp and
# friends (topico 14 CRITERIO DE ACEITE "parser/protocolo possui testes sem
# QWidget").
add_library(traceview_protocol STATIC
    protocol/btpbackend.cpp
    protocol/btpbackend.h
    protocol/btpframe.cpp
    protocol/btpframe.h
    protocol/btphandshake.cpp
    protocol/btphandshake.h
    protocol/btpsession.cpp
    protocol/btpsession.h
    protocol/manifestclient.cpp
    protocol/manifestclient.h
    protocol/packedledecoder.cpp
    protocol/packedledecoder.h
    protocol/protocolrouter.cpp
    protocol/protocolrouter.h
    protocol/statusreport.cpp
    protocol/statusreport.h
    protocol/subscriptionmanager.cpp
    protocol/subscriptionmanager.h
    protocol/telemetrycatalog.cpp
    protocol/telemetrycatalog.h
    protocol/telemetryfieldrouter.cpp
    protocol/telemetryfieldrouter.h
    protocol/telemetrysample.h
)

target_include_directories(traceview_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_protocol PUBLIC
    Qt6::Core
    btp::codec
    traceview_backend
    traceview_telemetry
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
    core/debugchartswindow.cpp
    core/debugchartswindow.h
    core/layerspanel.cpp
    core/layerspanel.h
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

configure_file(
    ${CMAKE_SOURCE_DIR}/include/traceview/version.h.in
    ${CMAKE_BINARY_DIR}/generated/traceview/version.h
    @ONLY
)

# traceview_theme: palette/stylesheet definitions and the ThemeManager/
# FontManager singletons (public headers live in include/traceview so the
# app can reach them without depending on any other layer). FontManager is
# independent of ThemeManager (see include/traceview/fontmanager.h) but
# ships in the same "appearance" library since it's the same kind of
# QSettings-backed, menu-driven picker.
add_library(traceview_theme STATIC
    theme/palettes.cpp
    theme/palettes.h
    theme/stylesheet.cpp
    theme/stylesheet.h
    theme/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/include/traceview/thememanager.h
    theme/fontmanager.cpp
    ${CMAKE_SOURCE_DIR}/include/traceview/fontmanager.h
    ${CMAKE_SOURCE_DIR}/include/traceview/font.h
)

target_include_directories(traceview_theme PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_theme PUBLIC
    Qt6::Widgets
)

# traceview_locale: language/translation definitions and the LanguageManager
# singleton (public header lives in include/traceview so the app can reach
# it without depending on any other layer, same split as traceview_theme).
# Only QtCore -- LanguageManager just uses QObject/QTranslator/QSettings/
# QCoreApplication/QLibraryInfo, no widgets.
add_library(traceview_locale STATIC
    locale/languagemanager.cpp
    ${CMAKE_SOURCE_DIR}/include/traceview/languagemanager.h
    ${CMAKE_SOURCE_DIR}/include/traceview/locale.h
)

target_include_directories(traceview_locale PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_locale PUBLIC
    Qt6::Core
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
    telemetry/catalogtopicinfo.h
    telemetry/deviceinforecord.h
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
    backend/statusseverity.h
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
    protocol/channelseal.cpp
    protocol/channelseal.h
    protocol/clocksync.cpp
    protocol/clocksync.h
    protocol/hubbinder.cpp
    protocol/hubbinder.h
    protocol/commandclient.cpp
    protocol/commandclient.h
    protocol/keyderivation.cpp
    protocol/keyderivation.h
    protocol/logentry.h
    protocol/logfilereader.cpp
    protocol/logfilereader.h
    protocol/logseverity.h
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
    # See the top-level CMakeLists.txt: QtNetwork is here for
    # QPasswordDigestor, not for any network access -- traceview_ota (below)
    # links it separately for that.
    Qt6::Network
    btp::codec
    # channelseal.cpp is the only place in this app that calls into btp::aead
    # (mirrors bally_OS's RadioSeal/bally_dongle's DongleKeyStore convention:
    # one file per repo owns the crypto call, everything else goes through
    # it) -- channel B (key E, TraceView <-> robot through a hub) needs it.
    btp::aead
    traceview_backend
    traceview_telemetry
)

# traceview_project: .tvproj (de)serialization. No dependency on the UI or
# dashboard layers.
add_library(traceview_project STATIC
    project/projectstore.cpp
    project/projectstore.h
    project/workspacemanager.cpp
    project/workspacemanager.h
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
    dashboard/widgetconfigeditor.cpp
    dashboard/widgetconfigeditor.h
    dashboard/widgets/chartconfigeditor.cpp
    dashboard/widgets/chartconfigeditor.h
    dashboard/widgets/chartdata.cpp
    dashboard/widgets/chartdata.h
    dashboard/widgets/chartwidgets.cpp
    dashboard/widgets/chartwidgets.h
    dashboard/widgets/gaugeconfigeditor.cpp
    dashboard/widgets/gaugeconfigeditor.h
    dashboard/widgets/serialmonitorconfigeditor.cpp
    dashboard/widgets/serialmonitorconfigeditor.h
    dashboard/widgets/serialmonitorwidget.cpp
    dashboard/widgets/serialmonitorwidget.h
    dashboard/widgets/serialterminalwidget.cpp
    dashboard/widgets/serialterminalwidget.h
    dashboard/widgets/textboardconfigeditor.cpp
    dashboard/widgets/textboardconfigeditor.h
    dashboard/widgets/textboardwidget.cpp
    dashboard/widgets/textboardwidget.h
    dashboard/widgets/terminaltabbar.cpp
    dashboard/widgets/terminaltabbar.h
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

# traceview_devices: the Devices panel's data model, card chrome, and fixed
# auto-flow grid. UI-only for now (mock/local data, no real transport
# wiring) -- a later phase wires real BTP device discovery into the same
# Device struct. Depends on traceview_theme so that data-source swap won't
# require touching this layer's visual code, and on traceview_telemetry
# (QtCore-only generic value types, see lib/telemetry/catalogtopicinfo.h) so
# DeviceConfigDialog can display a device's reported topic catalog without
# pulling in traceview_protocol/BTP -- same reasoning as the port list
# provider callback (traceview_devices doesn't depend on QSerialPort either).
add_library(traceview_devices STATIC
    devices/device.cpp
    devices/device.h
    devices/devicecard.cpp
    devices/devicecard.h
    devices/devicecommands.cpp
    devices/devicecommands.h
    devices/devicesgrid.cpp
    devices/devicesgrid.h
    devices/hubpeeraccumulator.cpp
    devices/hubpeeraccumulator.h
    devices/deviceconfigdialog.cpp
    devices/deviceconfigdialog.h
)

target_include_directories(traceview_devices PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_devices
    PUBLIC
        Qt6::Widgets
        traceview_telemetry
    PRIVATE
        traceview_theme
)

# traceview_logs: the Logs tab's content -- LogViewer, a read-only table of
# LogEntry rows produced by LogFileReader. No undo stack, no .tvproj
# persistence -- opening a file is the only state it holds.
add_library(traceview_logs STATIC
    logs/logviewer.cpp
    logs/logviewer.h
)

target_include_directories(traceview_logs PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_logs
    PUBLIC
        Qt6::Widgets
        traceview_protocol
)

# traceview_diagnostics: the two diagnostics surfaces -- the Notification
# History window (a replayable log of the status-bar messages MainWindow shows
# and lets scroll away) and the BTP Traffic Monitor tab (a live table of every
# BTP frame each device connection sends/receives, with a per-frame hex view
# and a password-driven Decrypt box for channel-B payloads). NotificationLog
# and FrameLog are the app-wide in-memory buffers MainWindow owns and feeds;
# the widgets render them. Depends on traceview_protocol for BtpFrame,
# ChannelSeal and deriveChannelKey, and on traceview_theme for severity
# colours -- same layering as traceview_logs.
add_library(traceview_diagnostics STATIC
    diagnostics/notificationentry.h
    diagnostics/notificationlog.cpp
    diagnostics/notificationlog.h
    diagnostics/notificationhistorywindow.cpp
    diagnostics/notificationhistorywindow.h
    diagnostics/framelogentry.h
    diagnostics/framelog.cpp
    diagnostics/framelog.h
    diagnostics/hexdump.cpp
    diagnostics/hexdump.h
    diagnostics/btpmonitortab.cpp
    diagnostics/btpmonitortab.h
)

target_include_directories(traceview_diagnostics PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_diagnostics
    PUBLIC
        Qt6::Widgets
        traceview_protocol
    PRIVATE
        traceview_theme
)

# traceview_ota: the OTA Update tab's content -- OtaClient (a
# QNetworkAccessManager wrapper talking to bally_OS's OTAUpdater HTTP side
# channel: GET /status, POST /update) and OtaTab, the table of devices it
# drives. Depends on traceview_devices for Device (otaAddress/otaPassword/
# cacheOtaPassword) and directly on Qt6::Network -- the first library in
# this app that actually uses it for network access rather than just
# QPasswordDigestor (see traceview_protocol's own link comment above).
add_library(traceview_ota STATIC
    ota/mdnsresolver.cpp
    ota/mdnsresolver.h
    ota/otaclient.cpp
    ota/otaclient.h
    ota/otatab.cpp
    ota/otatab.h
)

target_include_directories(traceview_ota PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_ota
    PUBLIC
        Qt6::Widgets
        Qt6::Network
        traceview_devices
)

# traceview_ui: MainWindow/Ribbon/AboutDialog — application chrome and
# coordination. Depends on the dashboard/project/theme layers to wire them
# together; nothing else is allowed to depend on traceview_ui.
add_library(traceview_ui STATIC
    core/aboutdialog.cpp
    core/aboutdialog.h
    core/shortcutsdialog.cpp
    core/shortcutsdialog.h
    core/donatedialog.cpp
    core/donatedialog.h
    core/debugchartswindow.cpp
    core/debugchartswindow.h
    core/dockablepanel.cpp
    core/dockablepanel.h
    core/dockablepanelheader.cpp
    core/dockablepanelheader.h
    core/dockdropindicator.cpp
    core/dockdropindicator.h
    core/dockresizegrip.cpp
    core/dockresizegrip.h
    core/layerspanel.cpp
    core/layerspanel.h
    core/mainwindow.cpp
    core/mainwindow.h
    core/paneldockcontroller.cpp
    core/paneldockcontroller.h
    core/propertiespanel.cpp
    core/propertiespanel.h
    core/ribbon.cpp
    core/ribbon.h
    core/ribbonicons.cpp
    core/ribbonicons.h
    core/ribbontabbar.cpp
    core/ribbontabbar.h
    core/deviceconnection.cpp
    core/deviceconnection.h
    core/hubtransport.cpp
    core/hubtransport.h
    core/serialmanager.cpp
    core/serialmanager.h
    core/serialwidgetbridge.cpp
    core/serialwidgetbridge.h
    core/transport.h
    core/usbhidmanager.cpp
    core/usbhidmanager.h
    core/workspaceswitcher.cpp
    core/workspaceswitcher.h
    vendor/qrcodegen/qrcodegen.cpp
    vendor/qrcodegen/qrcodegen.hpp
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
        traceview_devices
        traceview_diagnostics
        traceview_logs
        traceview_ota
        traceview_project
        traceview_protocol
        traceview_theme
        traceview_locale
        # UsbHidManager (core/usbhidmanager.h) is the only consumer -- a
        # transport-layer implementation detail, so PRIVATE (nothing above
        # traceview_ui needs to see hidapi's own headers).
        hidapi::hidapi
)

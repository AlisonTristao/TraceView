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
    theme/busyspinner.cpp
    theme/busyspinner.h
    theme/dialogpresenter.cpp
    theme/dialogpresenter.h
    theme/iconutils.cpp
    theme/iconutils.h
    theme/iconlibrary.cpp
    theme/iconlibrary.h
)

target_include_directories(traceview_theme PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_theme PUBLIC
    Qt6::Widgets
    Qt6::Svg
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

# traceview_preferences: typed, application-level options shared by multiple
# presentation surfaces. It deliberately stays QtCore-only so dashboard and
# diagnostics can consume it without depending on the MainWindow/UI target.
add_library(traceview_preferences STATIC
    preferences/appsettings.cpp
    preferences/appsettings.h
)

target_include_directories(traceview_preferences PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_preferences PUBLIC
    Qt6::Core
)

# traceview_applog: the app-wide file logger (AppLog) and its logging
# categories (lcSerial/lcUsbHid/lcConnection/lcApp). Split out of traceview_ui
# (topico T07, TAREFAS_TCP_BLE_ANDROID.txt) because traceview_transport below
# needs it too (SerialManager/UsbHidManager/DeviceConnection all log through
# it) and Q_LOGGING_CATEGORY's storage must be compiled exactly once --
# traceview_ui and traceview_transport both link this instead of one of them
# compiling applog.cpp itself, which would either duplicate the symbol or
# force a dependency cycle between the two. Only QtCore.
add_library(traceview_applog STATIC
    core/applog.cpp
    core/applog.h
)

target_include_directories(traceview_applog PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_applog PUBLIC
    Qt6::Core
)

# traceview_hexdump: hex-dump formatting helpers (hexDump/hexInline) for raw
# byte payloads. Split out of traceview_diagnostics for the same reason as
# traceview_applog above: traceview_transport's SerialManager needs it too,
# and traceview_diagnostics links Qt6::Widgets publicly, which transport must
# not pull in. Only QtCore.
add_library(traceview_hexdump STATIC
    diagnostics/hexdump.cpp
    diagnostics/hexdump.h
)

target_include_directories(traceview_hexdump PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_hexdump PUBLIC
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
    telemetry/telemetryhistory.cpp
    telemetry/telemetryhistory.h
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
    protocol/manifeststore.cpp
    protocol/manifeststore.h
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
    # btp::messages: struct <-> bytes for every COMMAND / CONTROL payload
    # (HELLO, MANIFEST_DATA, STATUS, SUBSCRIBE, COMMAND_*). Replaces the
    # hand-rolled offset parsing that used to live in statusreport.cpp,
    # btphandshake.cpp, manifestclient.cpp and friends. Links btp::codec,
    # no mbedtls.
    btp::messages
    # btp::telemetry: a TELEMETRY sample body (PACKED_LE / TLV_LE) against a
    # schema, replacing the hand-rolled packedledecoder.cpp.
    # TelemetryFieldRouter feeds it the FieldSpec array TelemetryCatalog keeps.
    btp::telemetry
    # btp::receiver: btp::decode + CRC + btp::Reassembler + the timeout sweep
    # behind one submit(), replacing the hand-rolled reassembly loop in
    # btpsession.cpp (BtpSession::handleReassembly). Links btp::codec only.
    btp::receiver
    # btp::node: BtpBackend's HELLO handshake (m_node->connect()) and its
    # COMMAND_REQUEST/RESULT client (CommandClient), plus the one shared
    # sequence counter every sealed send draws from (nextEndpointSequence()) --
    # see btpbackend.h's class comment for what it does and does not manage.
    # Links btp::endpoint/receiver/session/subscription/catalog transitively;
    # none of the discovery/subscription-server/command-server halves are
    # ever enabled here.
    btp::node
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
    project/dashboardgallery.cpp
    project/dashboardgallery.h
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
    dashboard/widgets/chatconfigeditor.cpp
    dashboard/widgets/chatconfigeditor.h
    dashboard/widgets/chatwidget.cpp
    dashboard/widgets/chatwidget.h
    dashboard/widgets/robotlogconfigeditor.cpp
    dashboard/widgets/robotlogconfigeditor.h
    dashboard/widgets/robotlogmodel.cpp
    dashboard/widgets/robotlogmodel.h
    dashboard/widgets/robotlogwidget.cpp
    dashboard/widgets/robotlogwidget.h
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
        traceview_preferences
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
    devices/devicelinkstable.cpp
    devices/robotlinkspanel.cpp
    devices/robotlinkspanel.h
    devices/devicelinkstable.h
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
    PRIVATE
        # BusySpinner (theme/busyspinner.h) only -- the corner "loading"
        # indicator shown while openFile()'s parse runs on a background
        # thread.
        traceview_theme
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
        traceview_preferences
        traceview_hexdump
)

# traceview_ota: the OTA Update tab's content -- OtaClient (a
# QNetworkAccessManager wrapper talking to bally_OS's OTAUpdater HTTP side
# channel: GET /status, POST /update) and OtaTab, the table of devices it
# drives. Depends on traceview_devices for Device (otaAddress/otaPassword/
# cacheOtaPassword) and directly on Qt6::Network -- the first library in
# this app that actually uses it for network access rather than just
# QPasswordDigestor (see traceview_protocol's own link comment above).
# traceview_mdns: the one-shot *.local resolver (ota/mdnsresolver.*), on its
# own so the TCP transport can resolve a robot's mDNS name without linking
# the whole OTA tab (Widgets, devices, theme). Windows has no mDNS resolver
# of its own -- see the class comment.
add_library(traceview_mdns STATIC
    ota/mdnsresolver.cpp
    ota/mdnsresolver.h
)

target_include_directories(traceview_mdns PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_mdns PUBLIC Qt6::Network)

add_library(traceview_ota STATIC
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
        traceview_mdns
    PRIVATE
        # DialogPresenter (theme/dialogpresenter.h) only -- OtaTab's upload
        # warnings go through it like every other dialog in the app.
        traceview_theme
)

# traceview_diagram: the Control tab's content -- a Simulink-style block
# diagram (DiagramScene/DiagramView/DiagramBlockItem) where each block mirrors
# a Device from the Devices tab, one per device automatically, and exposes
# its 3 BTP channels (Command/Terminal/Telemetry) as ports -- visual only,
# connections carry no runtime data yet. Each block also owns a live
# DiagramScriptRuntime (QJSEngine): MainWindow feeds it that device's
# telemetry/terminal traffic and relays its sendCommand/sendTerminal calls
# back to the real Backend, so the script genuinely reacts to and drives that
# device -- see diagramscriptruntime.h. No .tvproj persistence yet (see
# DiagramBlockConfigDialog's own note).
# Depends on traceview_devices for Device/deviceLinkState (the same struct the
# Devices tab's cards read), traceview_theme for painting (same reasoning as
# traceview_devices's own link comment), and Qt6::Qml for QJSEngine/QJSValue
# only -- no QML files, no scene graph.
add_library(traceview_diagram STATIC
    diagram/diagramport.h
    diagram/diagramportitem.cpp
    diagram/diagramportitem.h
    diagram/diagramblockitem.cpp
    diagram/diagramblockitem.h
    diagram/diagramconnectionitem.cpp
    diagram/diagramconnectionitem.h
    diagram/diagramscene.cpp
    diagram/diagramscene.h
    diagram/diagramview.cpp
    diagram/diagramview.h
    diagram/diagramscriptruntime.cpp
    diagram/diagramscriptruntime.h
    diagram/diagramblockconfigdialog.cpp
    diagram/diagramblockconfigdialog.h
    diagram/diagrampage.cpp
    diagram/diagrampage.h
)

target_include_directories(traceview_diagram PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_diagram
    PUBLIC
        Qt6::Widgets
        traceview_devices
        # DiagramScriptRuntime::handleStatus() takes a
        # traceview::StatusSeverity (backend/statusseverity.h) in its public
        # signature -- a real API dependency, not just an impl detail
        # traceview_backend's own header-only nature would let slide.
        traceview_backend
    PRIVATE
        traceview_theme
        Qt6::Qml
)

# traceview_updater: GitHub-Releases-based self-update. UpdateChecker (GET
# .../releases, semver compare against traceview::kVersion*), UpdateDownloader
# (fetches the matching platform asset + SHA256SUMS.txt and verifies the hash
# before releasing the file) and UpdateInstaller (platform split: runs the
# downloaded NSIS installer on Windows, extracts+swaps+relaunches on Linux).
# Needs the generated version.h (see configure_file above) for the local
# version to compare against, same as traceview_ui below.
add_library(traceview_updater STATIC
    updater/semver.cpp
    updater/semver.h
    updater/updateinfo.h
    updater/updatechecker.cpp
    updater/updatechecker.h
    updater/updatedownloader.cpp
    updater/updatedownloader.h
    updater/updateinstaller.cpp
    updater/updateinstaller.h
)

target_include_directories(traceview_updater PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(traceview_updater PUBLIC
    Qt6::Core
    Qt6::Network
)

# traceview_transport: the transport/connection layer -- Transport (the
# common bytes-in/bytes-out interface), SerialManager, UsbHidManager,
# HubTransport (the third Transport, multiplexed over a parent device's
# connection) and DeviceConnection (owns one device's Transport+Backend
# pair). Split out of traceview_ui (topico T07, TAREFAS_TCP_BLE_ANDROID.txt,
# ETAPA 2 "biblioteca de comunicacao compila sem Qt Widgets") so a headless
# consumer -- an Android build, a CLI tool, a unit test -- can link the wire
# protocol without pulling in QtWidgets at all. None of these 5 files ever
# used a QWidget or a project widget type (DashboardGrid/SerialMonitorWidget/
# RobotLogWidget); the only genuinely widget-coupled file that used to sit
# next to them, core/serialwidgetbridge.cpp, stays in traceview_ui below.
# Only QtCore + QtSerialPort (SerialManager's public header embeds a
# QSerialPort member and a QSerialPort::SerialPortError slot parameter, so
# QtSerialPort has to be a PUBLIC dependency here, same as traceview_ui's own
# link further down) -- hidapi and the BTP-aware libraries below are private
# implementation details of the .cpp files, not exposed by any public header
# in this target.
set(TRACEVIEW_TRANSPORT_SOURCES
    core/transport.h
    core/deviceconnection.cpp
    core/deviceconnection.h
    core/hubtransport.cpp
    core/hubtransport.h
    core/tcptransport.cpp
    core/tcptransport.h
)
# Same reasoning as the BLE sources below: serialmanager.h/usbhidmanager.h
# #include <QSerialPort>/hidapi.h unconditionally, so compiling them without
# the option that pulls in that dependency (see TRACEVIEW_ENABLE_SERIAL/
# TRACEVIEW_ENABLE_USB_HID's own link guards further down) fails outright --
# first surfaced building the android-arm64 preset, where neither module
# exists for the target ABI at all.
#
# TRACEVIEW_ENABLE_SERIAL means "TransportType::Serial exists", not "uses
# QSerialPort": Qt has no QSerialPort backend on Android, so there the same
# SerialTransport interface (core/serialtransport.h) is implemented over the
# Android USB Host API instead (core/androidusbserialtransport.cpp, talking
# to android/src/.../UsbSerialBridge.java over JNI).
if(TRACEVIEW_ENABLE_SERIAL)
    list(APPEND TRACEVIEW_TRANSPORT_SOURCES
        core/serialtransport.cpp
        core/serialtransport.h
    )
    if(ANDROID)
        list(APPEND TRACEVIEW_TRANSPORT_SOURCES
            core/androidusbserialtransport.cpp
            core/androidusbserialtransport.h
        )
    else()
        list(APPEND TRACEVIEW_TRANSPORT_SOURCES
            core/serialmanager.cpp
            core/serialmanager.h
        )
    endif()
endif()
if(TRACEVIEW_ENABLE_USB_HID)
    list(APPEND TRACEVIEW_TRANSPORT_SOURCES
        core/usbhidmanager.cpp
        core/usbhidmanager.h
    )
endif()
# blediscoveryservice/bletransport #include <QLowEnergyController> and
# friends directly -- unlike tcptransport.cpp (QTcpSocket lives in Qt6::
# Network, already an unconditional dependency of this whole project, see
# root CMakeLists.txt), those headers only exist when Qt6::Bluetooth was
# actually found, which only happens `if(TRACEVIEW_ENABLE_BLE)` there.
# Compiling them unconditionally would break a BLE-disabled configuration
# outright (T26's acceptance: "builds com BLE ativado e desativado resolvem
# dependências"), so the two files themselves -- not just the link below --
# are conditional.
if(TRACEVIEW_ENABLE_BLE)
    list(APPEND TRACEVIEW_TRANSPORT_SOURCES
        core/blediscoveryservice.cpp
        core/blediscoveryservice.h
        core/blepermission.cpp
        core/blepermission.h
        core/bletransport.cpp
        core/bletransport.h
    )
endif()

add_library(traceview_transport STATIC ${TRACEVIEW_TRANSPORT_SOURCES})

target_include_directories(traceview_transport PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(traceview_transport
    PUBLIC
        Qt6::Core
    PRIVATE
        # TcpTransport resolves *.local hosts itself (the robot's IP changes
        # on every reboot; its mDNS name does not).
        traceview_mdns
        # deviceconnection.cpp/hubtransport.cpp build/own the BtpBackend for
        # a device's session (BtpSession::Framing, btp::TransportLimits);
        # neither symbol appears in a public header of this target, so this
        # stays private -- traceview_ui and anything else calling
        # DeviceConnection::backend() already links traceview_protocol
        # itself to get BtpBackend's full definition.
        traceview_protocol
        # deviceconnection.cpp wires Transport::dataReceived/bytesToWrite
        # against the full Backend interface. Same "private, .cpp only"
        # reasoning as traceview_protocol above.
        traceview_backend
        # AppSettings::instance() (autoReconnect/reconnectIntervalSeconds) --
        # deviceconnection.cpp and serialmanager.cpp only.
        traceview_preferences
        traceview_applog
        traceview_hexdump
)

if(TRACEVIEW_ENABLE_SERIAL AND NOT ANDROID)
    target_link_libraries(traceview_transport PUBLIC Qt6::SerialPort)
    if(WIN32)
        # SerialManager::portProductNames() reads each port's USB
        # bus-reported description through SetupAPI.
        target_link_libraries(traceview_transport PRIVATE setupapi)
    endif()
endif()
if(TRACEVIEW_ENABLE_USB_HID)
    target_link_libraries(traceview_transport PRIVATE hidapi::hidapi)
endif()
if(TRACEVIEW_ENABLE_BLE)
    # PUBLIC for the same reason SerialPort is above: bletransport.h embeds
    # QLowEnergyCharacteristic/QLowEnergyService members and a QBluetoothUuid
    # in its own public interface, so anything that includes it (this
    # target's own deviceconnection.h does not, but a future direct consumer
    # could) needs the include paths too.
    target_link_libraries(traceview_transport PUBLIC Qt6::Bluetooth)
endif()

# usermodemanager.cpp's seeded developer-account credentials come from here
# rather than being hardcoded (see TRACEVIEW_DEFAULT_ADMIN_USER/_PASSWORD in
# the root CMakeLists.txt) -- generated per build into the binary dir, never
# committed. core/usermodedefaults.h.in is the tracked template.
configure_file(core/usermodedefaults.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/usermodedefaults.h @ONLY)

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
    core/updateavailabledialog.cpp
    core/updateavailabledialog.h
    core/debugchartswindow.cpp
    core/debugchartswindow.h
    core/devicepreviewframe.cpp
    core/devicepreviewframe.h
    core/dockablepanel.cpp
    core/dockablepanel.h
    core/dockablepanelheader.cpp
    core/dockablepanelheader.h
    core/dockdropindicator.cpp
    core/dockdropindicator.h
    core/dockresizegrip.cpp
    core/dockresizegrip.h
    core/fontmenuaction.cpp
    core/fontmenuaction.h
    core/layerspanel.cpp
    core/layerspanel.h
    core/logindialog.cpp
    core/logindialog.h
    core/mainwindow.cpp
    core/mainwindow.h
    core/manageusersdialog.cpp
    core/manageusersdialog.h
    core/dashboardgallerydialog.cpp
    core/dashboardgallerydialog.h
    core/settingspage.cpp
    core/settingspage.h
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
    core/serialwidgetbridge.cpp
    core/serialwidgetbridge.h
    core/usermodemanager.cpp
    core/usermodemanager.h
    core/workspaceswitcher.cpp
    core/workspaceswitcher.h
    core/workspacedock.cpp
    core/workspacedock.h
    core/touchscroll.cpp
    core/touchscroll.h
    core/inputdiagnostics.cpp
    core/inputdiagnostics.h
    core/iconpickerdialog.cpp
    core/iconpickerdialog.h
    core/brandcornermark.cpp
    core/brandcornermark.h
    core/startuploadingoverlay.cpp
    core/startuploadingoverlay.h
    vendor/qrcodegen/qrcodegen.cpp
    vendor/qrcodegen/qrcodegen.hpp
)

target_include_directories(traceview_ui PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_BINARY_DIR}/generated
    # usermodedefaults.h, generated by the configure_file() call above.
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(traceview_ui
    PUBLIC
        Qt6::Widgets
    PRIVATE
        traceview_dashboard
        traceview_devices
        traceview_diagnostics
        traceview_diagram
        # MainWindow includes diagram/diagramscriptruntime.h directly (owns
        # one DiagramScriptRuntime per device) -- QJSValue in that header's
        # own private interface means anything including it needs Qt6::Qml
        # too, not just traceview_diagram's own .cpp files.
        Qt6::Qml
        traceview_logs
        traceview_ota
        traceview_project
        traceview_protocol
        traceview_theme
        traceview_locale
        traceview_preferences
        traceview_updater
        # Transport/DeviceConnection/SerialManager/UsbHidManager/HubTransport
        # (see traceview_transport's own comment above) -- MainWindow owns one
        # DeviceConnection per Device and SerialWidgetBridge wires
        # SerialManager's writeCommand()/lineTerminator() to the raw
        # control/terminal widgets.
        traceview_transport
        # AppLog::install()/currentLogFilePath()/logDirectory() -- called from
        # main.cpp/mainwindow.cpp/settingspage.cpp (the "Open log folder"
        # action). Compiled once in traceview_transport above already; this is
        # the same target under a different consumer.
        traceview_applog
)

if(TRACEVIEW_ENABLE_SERIAL AND NOT ANDROID)
    target_link_libraries(traceview_ui PUBLIC Qt6::SerialPort)
endif()

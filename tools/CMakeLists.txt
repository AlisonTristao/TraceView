# Dev-only tools -- not part of the shipped app or the ctest suite.
# chart_preview/chart_benchmark are throwaway GUI harnesses for eyeballing a
# widget's design with synthetic data instead of a real serial device;
# synthetic_device is a CLI tool that goes the other way, speaking the real
# wire protocol so the app can be tested against a fake device.

add_executable(chart_preview chart_preview/main.cpp)
add_executable(chart_benchmark chart_benchmark/main.cpp)
# Speaks the device side of BTP v1 over a real QSerialPort (docs/
# SYNTHETIC_DEVICE.md) -- unlike the two GUI tools above, which inject
# synthetic values straight into widget APIs, this one actually goes on the
# wire, so it links traceview_protocol (BtpSession/ProtocolRouter/btp::codec)
# and Qt6::SerialPort instead of the dashboard/theme libraries. Headless
# QCoreApplication, no Qt6::Widgets. syntheticdevicesession.* is the generic
# device-role protocol engine (handshake/manifest/subscribe/watchdog);
# solarpaneldevice.*/weatherstationdevice.* are concrete device profiles
# plugged into it (--profile at the CLI) -- run one instance per profile/port
# to simulate several distinct, independently-connected devices at once.
add_executable(synthetic_device
    synthetic_device/main.cpp
    synthetic_device/syntheticdevicesession.cpp
    synthetic_device/syntheticdevicesession.h
    synthetic_device/solarpaneldevice.cpp
    synthetic_device/solarpaneldevice.h
    synthetic_device/weatherstationdevice.cpp
    synthetic_device/weatherstationdevice.h
    synthetic_device/wireutil.cpp
    synthetic_device/wireutil.h
)

target_link_libraries(chart_preview PRIVATE
    Qt6::Widgets
    traceview_dashboard
    traceview_theme
)

target_link_libraries(chart_benchmark PRIVATE
    Qt6::Widgets
    traceview_dashboard
    traceview_theme
)

target_link_libraries(synthetic_device PRIVATE
    Qt6::Core
    Qt6::SerialPort
    traceview_protocol
)

if(WIN32)
    # Same reasoning as TraceView's own post-build step (see root
    # CMakeLists.txt): without deployed DLLs next to the exe, Windows' DLL
    # search falls through to PATH and can pick up an ABI-incompatible Qt
    # build (e.g. an msvc2022_64 Qt install ahead of mingw_64 on PATH),
    # failing to start with STATUS_ENTRY_POINT_NOT_FOUND.
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET chart_preview POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:chart_preview>"
            COMMENT "Deploying Qt runtime DLLs next to chart_preview.exe"
        )
        add_custom_command(TARGET chart_benchmark POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:chart_benchmark>"
            COMMENT "Deploying Qt runtime DLLs next to chart_benchmark.exe"
        )
        add_custom_command(TARGET synthetic_device POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:synthetic_device>"
            COMMENT "Deploying Qt runtime DLLs next to synthetic_device.exe"
        )
    endif()
endif()

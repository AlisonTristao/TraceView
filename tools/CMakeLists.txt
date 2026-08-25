# Dev-only tools -- not part of the shipped app or the ctest suite.
# chart_preview/chart_benchmark are throwaway GUI harnesses for eyeballing a
# widget's design with synthetic data instead of a real serial device.

add_executable(chart_preview chart_preview/main.cpp)
add_executable(chart_benchmark chart_benchmark/main.cpp)

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
    endif()
endif()

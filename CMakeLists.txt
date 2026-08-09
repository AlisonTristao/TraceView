cmake_minimum_required(VERSION 3.21)

project(TraceView
    VERSION 0.1.0
    DESCRIPTION "Telemetry dashboard for line-following robots"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(TRACEVIEW_BUILD_TESTS "Build the Qt Test suite under tests/" ON)

if(WIN32)
    enable_language(RC)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)

add_subdirectory(lib)

if(TRACEVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

qt_add_executable(TraceView
    src/main.cpp
    resources/icons/app.qrc
)

if(WIN32)
    target_sources(TraceView PRIVATE resources/app.rc)
endif()

target_include_directories(TraceView PRIVATE include)

target_link_libraries(TraceView PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    traceview_ui
    traceview_theme
)

set_target_properties(TraceView PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(WIN32)
    # Keeps the Qt runtime DLLs sitting next to TraceView.exe in sync with
    # whatever Qt modules it links. Without this, adding a new Qt module
    # (e.g. Qt6SerialPort for the serial backend) silently leaves the app
    # unable to start (STATUS_DLL_NOT_FOUND) until someone re-deploys by
    # hand -- there was no deployment step at all before this, and the
    # build directory's DLL set had drifted stale for exactly that reason.
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET TraceView POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:TraceView>"
            COMMENT "Deploying Qt runtime DLLs next to TraceView.exe"
        )
    endif()
endif()

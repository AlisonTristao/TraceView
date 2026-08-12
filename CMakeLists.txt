cmake_minimum_required(VERSION 3.21)

project(TraceView
    VERSION 1.0.1
    DESCRIPTION "Real-time telemetry dashboard for ESP32/ESP-NOW robots"
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
option(TRACEVIEW_BUILD_TOOLS "Build dev-only visual harnesses under tools/" ON)

if(WIN32)
    enable_language(RC)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)

# BTP is the canonical, shared source of the BTP v1 wire format and codec
# (see BTP/PLANO_GERAL.txt) -- TraceView does not keep its own copy of the
# spec or the codec, it fetches the repository from git and links
# btp::codec, the same way the ESP32 firmwares pull it in via PlatformIO's
# lib_deps instead of a sibling checkout.
# BTP's own test suite (codec/transport/conformance vectors) is that
# repository's job to run, not TraceView's -- it also pulls in a Python3
# interpreter requirement for its vector-description check that TraceView
# otherwise has no reason to depend on. TraceView's tests/ directory covers
# the BTP integration from this repo's side instead (see
# tests/test_btpsession.cpp and friends).
include(FetchContent)
set(BTP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    btp
    GIT_REPOSITORY https://github.com/AlisonTristao/BTP.git
    GIT_TAG v1.0.1-beta
)
FetchContent_MakeAvailable(btp)

add_subdirectory(lib)

if(TRACEVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TRACEVIEW_BUILD_TOOLS)
    add_subdirectory(tools)
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

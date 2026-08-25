cmake_minimum_required(VERSION 3.21)

project(TraceView
    VERSION 2.3.0
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

# Network started out here for exactly one thing that was not networking:
# QPasswordDigestor::deriveKeyPbkdf2 (lib/protocol/keyderivation.cpp),
# which Qt places in QtNetwork rather than QtCore. Deriving the channel
# keys with the vetted implementation is worth the module, because the
# failure a hand-written PBKDF2 risks -- an almost-correct key -- surfaces
# two repositories away as a tag that will not verify, and looks like a
# wrong password or a radio fault rather than a bug here.
# traceview_ota (lib/ota/otaclient.cpp) is the first real use of the module
# for actual network traffic: QNetworkAccessManager against bally_OS's OTA
# HTTP side channel (GET /status, POST /update), a separate Wi-Fi link from
# the BTP/Serial/USB-HID/hub transport the rest of this app talks over.
find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network LinguistTools)

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
    GIT_TAG v2.0.0
)
FetchContent_MakeAvailable(btp)

# hidapi is the USB HID transport's backend (core/usbhidmanager.h, BTP's
# "usb_hid" profile) -- the same FetchContent pattern as BTP above, and
# pinned the same way: both dependencies are fetched at a fixed release tag
# rather than a rolling branch, so a build is reproducible. For hidapi that
# pin carries extra weight, because it has had behavior regressions between
# versions in the past (its own issue tracker), and a rolling `main` would
# land those silently. BUILD_SHARED_LIBS is forced off before fetching so
# hidapi builds as a static lib like every other internal/vendored piece
# here (see lib/CMakeLists.txt) instead of leaving a DLL for windeployqt to
# chase; hidapi's own default is ON (inherited from the global CMake
# variable) if this isn't set first.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG hidapi-0.15.0
)
FetchContent_MakeAvailable(hidapi)

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

qt_add_translations(TraceView
    TS_FILES
        translations/traceview_pt_BR.ts
        translations/traceview_es.ts
        translations/traceview_fr.ts
        translations/traceview_de.ts
        translations/traceview_it.ts
        translations/traceview_ru.ts
        translations/traceview_zh_CN.ts
        translations/traceview_ja.ts
    RESOURCE_PREFIX "/translations"
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
    traceview_locale
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
        # --no-translations removed: the app now ships real translations
        # (see LanguageManager), and it also installs Qt's own qtbase_*.qm
        # so native dialog chrome follows the app's language -- that file
        # has to be deployed next to the exe for QTranslator to find it.
        add_custom_command(TARGET TraceView POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:TraceView>"
            COMMENT "Deploying Qt runtime DLLs next to TraceView.exe"
        )
    endif()
endif()

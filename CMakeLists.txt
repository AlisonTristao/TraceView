cmake_minimum_required(VERSION 3.21)

project(TraceView
    VERSION 3.0.0
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
# Qml is here only for QJSEngine/QJSValue -- traceview_diagram's per-block
# script runtime (lib/diagram/diagramscriptruntime.h). No QML/.qml files, no
# scene graph: this is the lightweight "just the JS engine" half of the
# module, the same subset embedders reach for.
find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network Qml LinguistTools)

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
# btp::aead (AES-128-GCM/ChaCha20-Poly1305 over mbedtls) is what
# lib/protocol/channelseal.cpp uses to seal/open channel B (key E) traffic
# for a hub-channel device -- ON is BTP's own default, forced explicitly here
# so a build never silently loses it to some other scope setting the cache
# variable first.
set(BTP_ENABLE_AEAD ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    btp
    GIT_REPOSITORY https://github.com/AlisonTristao/BTP.git
    # v2.45.0: manifest format 3 field ranges are optional per field via
    # kFieldHasRange (see manifestclient.cpp and
    # dashboard/widgets/chartdata.h's ChartSeriesConfig::declaredMin/Max).
    GIT_TAG v2.45.0
)
FetchContent_MakeAvailable(btp)

# hidapi is the USB HID transport's backend (core/usbhidmanager.h, BTP's
# "usb_hid" profile) -- the same FetchContent pattern as BTP above, and
# pinned the same way: both dependencies are fetched at a fixed revision
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
        # --compiler-runtime: the local MinGW kit is Qt's own toolchain
        # (C:/Qt/Tools/mingw*_64), not the OS's -- a target machine has no
        # reason to already have libstdc++-6.dll / libgcc_s_seh-1.dll /
        # libwinpthread-1.dll on it, so the NSIS installer (see the CPack
        # block below) needs windeployqt to bundle them too, or the
        # packaged app fails to start with STATUS_DLL_NOT_FOUND on a clean
        # machine.
        add_custom_command(TARGET TraceView POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --compiler-runtime "$<TARGET_FILE:TraceView>"
            COMMENT "Deploying Qt + MinGW runtime next to TraceView.exe"
        )
    else()
        message(WARNING "windeployqt not found: TraceView.exe will not have "
            "Qt runtime DLLs deployed next to it, and a CPack/NSIS package "
            "built from this configure will be missing them too.")
    endif()
endif()

# -----------------------------------------------------------------------------
# Packaging (CPack) -- also produced by .github/workflows/release.yml on a
# vX.Y.Z tag push, which publishes the result as a GitHub Release (see
# CONTRIBUTING.md). CPACK_PACKAGE_FILE_NAME below is what the self-updater
# (lib/updater) matches a release asset against, so it must stay stable.
# Windows: NSIS generator -> setup.exe with TraceView.exe + Qt/MinGW runtime
# (windeployqt re-run against the staged install dir below, not the build
# dir -- TraceView is a root-level target, so $<TARGET_FILE_DIR:TraceView>
# is the build directory itself and also holds CMakeCache.txt/CMakeFiles/
# test and tool binaries/etc., none of which belong in the package).
# Linux: TGZ generator -> bin/TraceView + Qt (and Qt's own third-party libs)
# bundled into lib/ and plugins/ by scripts/collect_linux_deps.sh, the same
# "bundle everything except the OS itself" idea as windeployqt above -- see
# that script for exactly what does and doesn't get bundled and why. BTP and
# hidapi are fetched as static libs (BUILD_SHARED_LIBS forced OFF above), so
# only Qt itself needs collecting. macOS is out of scope here even though
# MACOSX_BUNDLE is set above.
# -----------------------------------------------------------------------------
if(WIN32)
    install(TARGETS TraceView RUNTIME DESTINATION .)

    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" --compiler-runtime
                \"\${CMAKE_INSTALL_PREFIX}/TraceView.exe\")
        ")
    endif()

    # windeployqt's --compiler-runtime only bundles libgcc/libstdc++/
    # libwinpthread -- it has no notion of a Qt module's OWN third-party
    # shared-library dependencies (e.g. a MinGW/MSYS2-built Qt6Network.dll
    # linking against a separate libbrotlidec DLL for HTTP decompression,
    # which the official Qt installer's kit doesn't need at all). Sweeping
    # the staged install directory with `ldd` afterwards catches whatever
    # windeployqt didn't -- see scripts/collect_windows_deps.sh's own
    # comment for the exact failure this fixes.
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        # bash.exe run as a bare subprocess (not an interactive/login shell)
        # never sources the profile that puts Git's own usr/bin -- where its
        # ldd.exe lives -- on PATH, so the script's `command -v ldd` check
        # fails silently whenever cpack itself is run from something other
        # than a Git Bash terminal (PowerShell locally, or CI's Windows
        # runner, which defaults its `run:` steps to PowerShell too). Prepend
        # it explicitly so the sweep actually runs everywhere cpack does.
        get_filename_component(BASH_BIN_DIR "${BASH_EXECUTABLE}" DIRECTORY)
        install(CODE "
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E env
                \"PATH=${BASH_BIN_DIR};\$ENV{PATH}\"
                \"${BASH_EXECUTABLE}\"
                \"${CMAKE_SOURCE_DIR}/scripts/collect_windows_deps.sh\"
                \"\${CMAKE_INSTALL_PREFIX}\")
        ")
    else()
        message(WARNING "bash not found: a CPack/NSIS package built from this "
            "configure may be missing a Qt module's own third-party DLL "
            "dependency (see scripts/collect_windows_deps.sh).")
    endif()
else()
    install(TARGETS TraceView RUNTIME DESTINATION bin)

    # qmake is Qt's own blessed way to ask "where are your plugins", and
    # covers every distro layout (Debian's /usr/lib/x86_64-linux-gnu/qt6/...,
    # Arch's /usr/lib/qt6/..., etc.) without hardcoding one.
    find_program(QMAKE_EXECUTABLE NAMES qmake6 qmake)
    find_program(PATCHELF_EXECUTABLE NAMES patchelf)
    # Invoked via bash explicitly (like collect_windows_deps.sh below), not
    # relying on the script's own +x bit -- this repo's checkouts are mostly
    # on Windows, where git file-mode tracking isn't reliable.
    find_program(BASH_EXECUTABLE bash)
    if(QMAKE_EXECUTABLE AND PATCHELF_EXECUTABLE AND BASH_EXECUTABLE)
        execute_process(
            COMMAND "${QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
            OUTPUT_VARIABLE QT_PLUGINS_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        install(CODE "
            execute_process(COMMAND \"${BASH_EXECUTABLE}\"
                \"${CMAKE_SOURCE_DIR}/scripts/collect_linux_deps.sh\"
                \"\${CMAKE_INSTALL_PREFIX}\" \"${QT_PLUGINS_DIR}\")
        ")
    else()
        message(WARNING "qmake/patchelf/bash not found: a CPack/TGZ package "
            "built from this configure will not bundle Qt, and will need a "
            "matching system Qt6 install on the target machine to run.")
    endif()
endif()

set(CPACK_PACKAGE_NAME "TraceView")
set(CPACK_PACKAGE_VENDOR "Alison Tristao")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TraceView")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_EXECUTABLES "TraceView" "TraceView")

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    # The .exe is installed to the package root above (next to windeployqt's
    # DLLs), not to a "bin" subfolder -- CPack/NSIS defaults to looking in
    # "bin" for the Start Menu shortcut target, so point it at "." instead.
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "TraceView.exe")
    set(CPACK_NSIS_DISPLAY_NAME "TraceView")
    set(CPACK_NSIS_PACKAGE_NAME "TraceView ${PROJECT_VERSION}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    # A fixed, predictable name (rather than CPack's own default) is what
    # the self-updater (lib/updater/updatechecker.cpp) matches a GitHub
    # release asset against, by suffix -- ".exe" here, ".tar.gz" below.
    set(CPACK_PACKAGE_FILE_NAME "TraceView-${PROJECT_VERSION}-windows-x64")
elseif(UNIX)
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_FILE_NAME "TraceView-${PROJECT_VERSION}-linux-x64")
endif()

include(CPack)

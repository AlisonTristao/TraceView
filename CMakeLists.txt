cmake_minimum_required(VERSION 3.21)

project(TraceView
    VERSION 4.6.3
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
option(TRACEVIEW_FLATPAK_BUILD "Use Flatpak-managed application updates" OFF)
# iOS has no serial and no USB HID transport, by platform rule rather than
# by missing code: iOS gives apps no access to USB-serial adapters (CP210x,
# CH340, the ESP32's own USB-CDC) or raw HID devices -- the only sanctioned
# route is Apple's MFi ExternalAccessory program, which needs an Apple
# authentication chip in the peripheral itself. On iOS, TraceView talks to
# devices over BLE, TCP or a hub channel only. See docs/MACOS_IOS_BUILD.md.
if(IOS)
    set(TRACEVIEW_WIRED_TRANSPORTS_DEFAULT OFF)
else()
    set(TRACEVIEW_WIRED_TRANSPORTS_DEFAULT ON)
endif()
option(TRACEVIEW_ENABLE_SERIAL "Build the serial transport (QSerialPort; USB Host API on Android; never on iOS)"
    ${TRACEVIEW_WIRED_TRANSPORTS_DEFAULT})
option(TRACEVIEW_ENABLE_USB_HID "Build the hidapi USB HID transport (never on iOS)"
    ${TRACEVIEW_WIRED_TRANSPORTS_DEFAULT})
option(TRACEVIEW_ENABLE_TCP "Enable the TCP transport integration" OFF)
# ON by default since 4.3.0: the transport (lib/core/bletransport.cpp) and
# the firmware's GATT service both exist; it was only left OFF while they
# were being written. Needs the Qt Connectivity module (Qt6::Bluetooth).
option(TRACEVIEW_ENABLE_BLE "Enable the BLE transport integration" ON)

if(IOS AND (TRACEVIEW_ENABLE_SERIAL OR TRACEVIEW_ENABLE_USB_HID))
    message(FATAL_ERROR
        "TRACEVIEW_ENABLE_SERIAL and TRACEVIEW_ENABLE_USB_HID must be OFF on iOS: "
        "iOS does not let apps talk to USB-serial or USB HID devices. Use BLE or "
        "TCP instead (see docs/MACOS_IOS_BUILD.md).")
endif()

# The developer-mode login seeded on every fresh install (see
# UserModeManager::seedDefaultAccount()) used to hardcode "admin233#" in
# usermodemanager.cpp -- trivially extractable from an Android APK. The
# value now comes from here instead, via lib/core/usermodedefaults.h.in, so
# a given deployment can set its own at configure time without touching
# tracked source. Left at the "admin" placeholder, MainWindow's Access menu
# warns that the default is still in use (see
# kDefaultAdminPasswordIsPlaceholder in the generated header).
set(TRACEVIEW_DEFAULT_ADMIN_USER "admin" CACHE STRING
    "Developer account username seeded on first run")
set(TRACEVIEW_DEFAULT_ADMIN_PASSWORD "admin" CACHE STRING
    "Password for the seeded developer account. Override at configure time per deployment.")

# Compared here rather than in the generated header: a C++-side check would
# have to compare string contents at compile time, and the obvious-looking
# sizeof() shortcut compares lengths instead -- any 5-character password
# would then masquerade as the shipped default and trip the warning.
if(TRACEVIEW_DEFAULT_ADMIN_PASSWORD STREQUAL "admin")
    set(TRACEVIEW_DEFAULT_ADMIN_PASSWORD_IS_PLACEHOLDER "true")
else()
    set(TRACEVIEW_DEFAULT_ADMIN_PASSWORD_IS_PLACEHOLDER "false")
endif()

if(TRACEVIEW_FLATPAK_BUILD)
    add_compile_definitions(TRACEVIEW_FLATPAK_BUILD)
endif()

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
# Svg is here for lib/theme/iconutils.cpp: every flat UI glyph (ribbon
# buttons, settings categories, dashboard cell header controls) is an SVG
# resource (resources/icons/ui.qrc) rendered via QSvgRenderer and tinted to
# the active theme color at runtime, instead of hand-drawn with QPainter.
set(TRACEVIEW_QT_COMPONENTS Widgets Network Qml Svg LinguistTools)
# Android's serial transport goes through the USB Host API instead (see
# lib/CMakeLists.txt) -- Qt ships no QSerialPort backend for it.
if(TRACEVIEW_ENABLE_SERIAL AND NOT ANDROID)
    list(APPEND TRACEVIEW_QT_COMPONENTS SerialPort)
endif()
if(TRACEVIEW_ENABLE_BLE)
    list(APPEND TRACEVIEW_QT_COMPONENTS Bluetooth)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${TRACEVIEW_QT_COMPONENTS})

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
if(TRACEVIEW_ENABLE_USB_HID)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        hidapi
        GIT_REPOSITORY https://github.com/libusb/hidapi.git
        GIT_TAG hidapi-0.15.0
    )
    FetchContent_MakeAvailable(hidapi)
endif()

if(TRACEVIEW_ENABLE_SERIAL)
    add_compile_definitions(TRACEVIEW_ENABLE_SERIAL)
endif()
if(TRACEVIEW_ENABLE_USB_HID)
    add_compile_definitions(TRACEVIEW_ENABLE_USB_HID)
endif()
if(TRACEVIEW_ENABLE_TCP)
    add_compile_definitions(TRACEVIEW_ENABLE_TCP)
endif()
if(TRACEVIEW_ENABLE_BLE)
    add_compile_definitions(TRACEVIEW_ENABLE_BLE)
endif()

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
    resources/icons/ui.qrc
    # Lucide (ISC), the user-pickable icon set -- see lib/theme/iconlibrary.h.
    resources/icons/lucide/lucide.qrc
    # Wordmark shown in User mode -- see lib/core/brandcornermark.h.
    resources/branding/branding.qrc
    # The built-in example dashboard the app starts on until the gallery has
    # a default of its own -- see lib/project/dashboardgallery.h.
    resources/projects/projects.qrc
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
    # Only compiled on Android, so lupdate run on a desktop build would never
    # see its strings and mark their translations vanished -- list it here.
    SOURCES
        lib/core/androidusbserialtransport.cpp
)

if(WIN32)
    target_sources(TraceView PRIVATE resources/app.rc)
endif()

target_include_directories(TraceView PRIVATE include)

target_link_libraries(TraceView PRIVATE
    Qt6::Widgets
    traceview_ui
    traceview_theme
    traceview_locale
)

if(TRACEVIEW_ENABLE_SERIAL AND NOT ANDROID)
    target_link_libraries(TraceView PRIVATE Qt6::SerialPort)
endif()

set_target_properties(TraceView PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(APPLE)
    # One bundle identity for macOS and iOS, matching the Flatpak/desktop
    # app id. XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER is what Xcode (the
    # iOS generator) signs against; the plist templates read
    # MACOSX_BUNDLE_GUI_IDENTIFIER.
    set_target_properties(TraceView PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "TraceView"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.alisontristao.TraceView"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Alison Tristao. MIT License."
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.alisontristao.TraceView"
    )
    if(IOS)
        # Own Info.plist for the Bluetooth/local-network usage descriptions
        # (see the template's own comment). The asset catalog carries the
        # app icon: a single opaque 1024px image, which Xcode 14+ scales to
        # every size itself.
        target_sources(TraceView PRIVATE resources/ios/Assets.xcassets)
        set_source_files_properties(resources/ios/Assets.xcassets PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(TraceView PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/ios/Info.plist.in"
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
            # iPhone and iPad.
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        )
    else()
        # The plist's LSMinimumSystemVersion. Qt 6.9 itself needs macOS 12+,
        # which is also what the macos-* presets pin.
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            set(TRACEVIEW_MACOS_MIN_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            set(TRACEVIEW_MACOS_MIN_VERSION "12.0")
        endif()
        # app.icns is generated from resources/icons/app.svg (1024px down to
        # 16px) -- regenerate it whenever the SVG changes.
        target_sources(TraceView PRIVATE resources/icons/app.icns)
        set_source_files_properties(resources/icons/app.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(TraceView PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "app.icns"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in"
        )
    endif()
endif()

if(ANDROID)
    # Without these, androiddeployqt falls back to its own placeholder
    # (org.qtproject.example.TraceView, versionName 1.0, versionCode 1) --
    # same package identity for every build regardless of what's actually
    # installed, so a newer APK can't even be told apart from an older one.
    # versionCode has to be an integer Android treats as monotonically
    # increasing across installs; MAJOR*10000 + MINOR*100 + PATCH leaves
    # room under each digit without ever colliding across versions.
    math(EXPR TRACEVIEW_ANDROID_VERSION_CODE
        "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
    set_target_properties(TraceView PROPERTIES
        QT_ANDROID_PACKAGE_NAME "io.github.alisontristao.traceview"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${TRACEVIEW_ANDROID_VERSION_CODE}"
        # Qt's own built-in manifest template (used when this isn't set)
        # sets no android:theme at all, so the OS falls back to its default
        # theme -- which includes a native ActionBar showing the app label
        # ("TraceView") across the top, on top of TraceView's own menu bar/
        # ribbon. android/AndroidManifest.xml is a copy of that same
        # template (all %%INSERT_...%% placeholders intact -- androiddeployqt
        # substitutes them at build time; hand-editing anything else here
        # risks breaking packaging, see docs/ANDROID_BUILD.md) with just
        # Theme.Black.NoTitleBar added to the activity and android:icon
        # pointed at @mipmap/ic_launcher (android/res/: an adaptive icon
        # built from resources/icons/app.svg, plus PNG fallbacks) -- left as
        # the placeholder, the APK ships with the generic Android icon.
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
        # Qt's Android kit ships the qopensslbackend TLS plugin
        # (plugins/tls/) but not the OpenSSL shared libraries it dlopen()s
        # at runtime -- Android has no system OpenSSL to link against the
        # way desktop Linux does, so without these every HTTPS request
        # (UpdateChecker's GitHub API polling included) fails outright.
        # Prebuilt from KDAB/android_openssl (the library Qt's own Android
        # docs point to for this) -- ssl_3 build, matching Qt 6.5+'s
        # OpenSSL 3.x requirement.
        QT_ANDROID_EXTRA_LIBS "${CMAKE_SOURCE_DIR}/android/openssl/arm64-v8a/libcrypto_3.so;${CMAKE_SOURCE_DIR}/android/openssl/arm64-v8a/libssl_3.so"
    )
    # The OpenSSL libraries above are useless without that TLS plugin, and
    # Qt only lists a module's plugins in android-deploy-plugins when the
    # executable links the module directly -- Qt6::Network reaching
    # TraceView only through traceview_* left plugins/tls/ out of the APK
    # entirely ("TLS initialization failed" on-device, despite libssl being
    # packaged).
    target_link_libraries(TraceView PRIVATE Qt6::Network)
endif()

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
# Linux: AppImage via scripts/build_linux_appimage.sh and linuxdeploy-plugin-qt.
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
    # BUNDLE is required for the macOS/iOS .app (a MACOSX_BUNDLE target):
    # without it, configure fails outright on Apple platforms. The macOS
    # .dmg itself is built by scripts/build_macos_dmg.sh, not CPack.
    install(TARGETS TraceView
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(FILES resources/linux/io.github.alisontristao.TraceView.desktop DESTINATION share/applications)
        install(FILES resources/linux/io.github.alisontristao.TraceView.metainfo.xml DESTINATION share/metainfo)
        install(FILES resources/icons/app_256.png
            DESTINATION share/icons/hicolor/256x256/apps RENAME io.github.alisontristao.TraceView.png)
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/appimage-version.txt"
            CONTENT "${PROJECT_VERSION}\n")
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
    # release asset against, by suffix -- ".exe" here, ".AppImage" on Linux.
    set(CPACK_PACKAGE_FILE_NAME "TraceView-${PROJECT_VERSION}-windows-x64")
    include(CPack)
endif()

# TraceLab: simulation, closed-loop control and animated scenes, built into
# TraceView as the optional "lab" module (see lab/README.md). Only included
# when TRACEVIEW_ENABLE_LAB is ON (never on Android or iOS).
#
# Dependency rule: these targets may link lib/ targets; no lib/ target may
# link one of these. Includes are rooted at the repository ("lab/sim/...").

# The plant and the controller host speak BTP 2.50: STREAM actions, action
# records in the Catalog, Node::send_stream(), the loop profile headers. That
# version exists only in a local BTP checkout for now (branch
# feature/stream-actions), so the lab needs FetchContent's override pointing
# at it (lab/README.md, "Building against a local BTP"):
#   cmake -S . -B build-lab -DFETCHCONTENT_SOURCE_DIR_BTP=C:/git/BTP
set(_tracelab_btp_version_hpp "${btp_SOURCE_DIR}/include/btp/version.hpp")
set(_tracelab_btp_minor 0)
if(EXISTS "${_tracelab_btp_version_hpp}")
    file(STRINGS "${_tracelab_btp_version_hpp}" _tracelab_btp_lines
         REGEX "kLibraryVersion(Major|Minor)[ 	]*=[ 	]*[0-9]+")
    string(REGEX MATCH "kLibraryVersionMajor[ 	]*=[ 	]*([0-9]+)" _ "${_tracelab_btp_lines}")
    set(_tracelab_btp_major "${CMAKE_MATCH_1}")
    string(REGEX MATCH "kLibraryVersionMinor[ 	]*=[ 	]*([0-9]+)" _ "${_tracelab_btp_lines}")
    set(_tracelab_btp_minor "${CMAKE_MATCH_1}")
endif()
if(NOT _tracelab_btp_major STREQUAL "2" OR _tracelab_btp_minor LESS 50)
    message(FATAL_ERROR
        "TraceLab (TRACEVIEW_ENABLE_LAB=ON) needs BTP >= 2.50 (STREAM actions, actions in "
        "the Catalog, Node::send_stream(), btp/profiles/loop.hpp), but the BTP fetched for "
        "this build is ${_tracelab_btp_major}.${_tracelab_btp_minor} (${btp_SOURCE_DIR}). "
        "BTP 2.50 is not published yet: point the build at a local checkout of its "
        "feature/stream-actions branch with -DFETCHCONTENT_SOURCE_DIR_BTP=<path to BTP>, "
        "or configure with -DTRACEVIEW_ENABLE_LAB=OFF.")
endif()

# tracelab_sim: the engine's contracts -- model, compiled ODE system, solver,
# scheduler, environments. Plain C++17, no Qt, so it can be tested alone and
# reused by tracelab-cli.
add_library(tracelab_sim STATIC
    sim/environment.cpp
    sim/environment.h
    sim/modelcompiler.cpp
    sim/modeldefinition.h
    sim/odesystem.h
    sim/sampleclock.h
    sim/scheduler.cpp
    sim/solver.cpp
    sim/solver.h
)
set_target_properties(tracelab_sim PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(tracelab_sim PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tracelab_sim PUBLIC cxx_std_17)

# Eigen 3.4 (MPL-2, header-only), used by tracelab_sensors and the
# line-follower controllers, pinned like BTP and hidapi in the root
# CMakeLists.txt. Only the sources are fetched: SOURCE_SUBDIR points at a
# folder with no CMakeLists.txt, so FetchContent_MakeAvailable() populates
# Eigen without add_subdirectory() -- none of its tests, docs, BLAS/LAPACK
# targets, pkg-config or uninstall target enter this build, and no global
# cache variable (BUILD_TESTING, ...) has to be forced to keep them out. A
# parent project that already provides Eigen3::Eigen is used as is.
if(NOT TARGET Eigen3::Eigen)
    include(FetchContent)
    FetchContent_Declare(
        tracelab_eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3.4.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR no-cmake-build
    )
    FetchContent_MakeAvailable(tracelab_eigen)
    add_library(tracelab_eigen INTERFACE)
    # SYSTEM: Eigen's own warnings are not this project's.
    target_include_directories(tracelab_eigen SYSTEM INTERFACE ${tracelab_eigen_SOURCE_DIR})
    # Refuse, at compile time, any Eigen header that is not MPL-2.
    target_compile_definitions(tracelab_eigen INTERFACE EIGEN_MPL2_ONLY)
    add_library(Eigen3::Eigen ALIAS tracelab_eigen)
endif()

# tracelab_numeric: generic numerics shared by the benchmarks' controllers
# and the code blocks' library (tracelab.h's tl_qp_box()...): the
# box-constrained QP. Plain C++17 and Eigen, no Qt. Optimized in Debug too:
# the MPCs run it every tick.
add_library(tracelab_numeric STATIC
    numeric/boxqp.cpp
    numeric/boxqp.h
)
set_target_properties(tracelab_numeric PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(tracelab_numeric PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tracelab_numeric PUBLIC cxx_std_17)
target_link_libraries(tracelab_numeric PUBLIC Eigen3::Eigen)
if(NOT MSVC)
    target_compile_options(tracelab_numeric PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
target_compile_definitions(tracelab_numeric PRIVATE $<$<CONFIG:Debug>:EIGEN_NO_DEBUG>)

# tracelab_sensors: generic sensor emulation -- reproducible noise streams,
# per-channel imperfections, linear (H x + D u) and nonlinear h(x, u, env)
# observation blocks. Simulation side only, never exported to firmware.
# Plain C++17 and Eigen, no Qt.
add_library(tracelab_sensors STATIC
    sensors/imperfections.cpp
    sensors/imperfections.h
    sensors/observation.cpp
    sensors/observation.h
    sensors/sensorrng.cpp
    sensors/sensorrng.h
)
set_target_properties(tracelab_sensors PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(tracelab_sensors PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tracelab_sensors PUBLIC cxx_std_17)
# PUBLIC: observation.h exposes Eigen types.
target_link_libraries(tracelab_sensors PUBLIC Eigen3::Eigen)

# tracelab_plant: the simulated plant as a BTP device, its manifest built from
# the model, and the in-process loopback transport.
add_library(tracelab_plant STATIC
    plant/btpproducerport.cpp
    plant/btpproducerport.h
    plant/codeplant.cpp
    plant/codeplant.h
    plant/codeplantdevice.cpp
    plant/codeplantdevice.h
    plant/endpointtransport.cpp
    plant/endpointtransport.h
    plant/labplantdevice.h
    plant/loopbacktransport.cpp
    plant/loopbacktransport.h
    plant/plantenvironment.h
    plant/plantmanifest.cpp
    plant/plantmanifest.h
    plant/simulatedplant.cpp
    plant/simulatedplant.h
)
target_include_directories(tracelab_plant PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_plant
    PUBLIC
        Qt6::Core
        tracelab_sim
        # traceview::Transport (lib/core/transport.h) and ModuleTransport
        # (lib/core/moduledevicetype.h).
        traceview_transport
        # BtpProducerPort is a btp::NodeConfig serving a btp::Catalog.
        btp::node
        btp::catalog
        # The plant written in C (codeplant.h, codeplantdevice.h).
        tracelab_code
)

# tracelab_code: plant, filter and controller blocks written in C, compiled in
# memory by libtcc (lab/code/CMakeLists.txt, lab/README.md "Code blocks").
add_subdirectory(code)

# tracelab_control: the controller host and the ways to program a controller.
add_library(tracelab_control STATIC
    control/codecontrolleradapter.cpp
    control/codecontrolleradapter.h
    control/codesignalfilter.cpp
    control/codesignalfilter.h
    control/controlleradapter.h
    control/controllerhost.cpp
    control/controllerhost.h
    control/dllcontrolleradapter.cpp
    control/dllcontrolleradapter.h
    control/labdevices.cpp
    control/labdevices.h
    control/scriptcontrolleradapter.cpp
    control/scriptcontrolleradapter.h
    control/signalfilter.h
)
target_include_directories(tracelab_control PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_control
    PUBLIC
        Qt6::Core
        # QJSEngine in scriptcontrolleradapter.h.
        Qt6::Qml
        traceview_transport
        # The consumer btp::Node toward the plant, and BtpProducerPort toward
        # TraceView.
        btp::node
        tracelab_plant
        # The controller and filters written in C (codecontrolleradapter.h,
        # codesignalfilter.h).
        tracelab_code
)

# tracelab_scenes: the SceneRenderer contract, the scene registry, the Scene
# dashboard widget with its config editor, and the scenes themselves.
add_library(tracelab_scenes STATIC
    scenes/diffdrivecamera.cpp
    scenes/diffdrivecamera.h
    scenes/diffdrivescene.cpp
    scenes/diffdrivescene.h
    scenes/sceneconfigeditor.cpp
    scenes/sceneconfigeditor.h
    scenes/scenerenderer.h
    scenes/sceneregistry.cpp
    scenes/sceneregistry.h
    scenes/scenewidget.cpp
    scenes/scenewidget.h
)
target_include_directories(tracelab_scenes PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_scenes
    PUBLIC
        Qt6::Widgets
        traceview_dashboard
    PRIVATE
        # The dashboard render rate (AppSettings::repaintIntervalMs()).
        traceview_preferences
        # DoubleSpinBox (theme/doublespinbox.h), the scene parameters' fields.
        traceview_theme
)

# tracelab_module: LabModule, what src/main.cpp registers.
add_library(tracelab_module STATIC
    module/blockeditor.cpp
    module/blockeditor.h
    module/codeeditor.cpp
    module/codeeditor.h
    module/externaleditor.cpp
    module/externaleditor.h
    module/labcard.cpp
    module/labcard.h
    module/labmodule.cpp
    module/labmodule.h
    module/labpanel.cpp
    module/labpanel.h
    module/plantpage.cpp
    module/plantpage.h
)
target_include_directories(tracelab_module PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_module
    PUBLIC
        traceview_modules
    PRIVATE
        Qt6::Widgets
        tracelab_scenes
        traceview_dashboard
        # LabControllerDevice and the code blocks the tab edits.
        tracelab_control
        tracelab_code
        # DoubleSpinBox (theme/doublespinbox.h), the theme's colors, frame
        # style and tinted icons for the tab's cards and ribbon buttons.
        traceview_theme
)

# Filters: the portable C filter runtime and its host-side IIR design
# (lab/README.md, "Filters").
add_subdirectory(filters)

# Benchmarks: a plant, its controllers, their BTP devices and a runner app per
# benchmark (lab/README.md, "Benchmarks").
add_subdirectory(benchmarks/linefollower)

# The line follower's devices and preset project in the TraceLab tab.
target_link_libraries(tracelab_module PRIVATE tracelab_lf_devices traceview_transport)
qt_add_resources(tracelab_module tracelab_presets
    PREFIX "/tracelab"
    BASE benchmarks/linefollower
    FILES benchmarks/linefollower/linefollower.tvproj
)
# The TraceLab tab's ribbon glyphs, same 16px grid as resources/icons/ribbon.
qt_add_resources(tracelab_module tracelab_icons
    PREFIX "/tracelab/icons"
    BASE module/icons
    FILES
        module/icons/code.svg
        module/icons/controller.svg
        module/icons/filter.svg
        module/icons/pause.svg
        module/icons/plant.svg
        module/icons/play.svg
        module/icons/reset.svg
        module/icons/scene.svg
        module/icons/step.svg
)

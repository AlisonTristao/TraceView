# TraceLab: simulation, closed-loop control and animated scenes, built into
# TraceView as the optional "lab" module (see lab/README.md). Only included
# when TRACEVIEW_ENABLE_LAB is ON (never on Android or iOS).
#
# Dependency rule: these targets may link lib/ targets; no lib/ target may
# link one of these. Includes are rooted at the repository ("lab/sim/...").

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
    plant/loopbacktransport.cpp
    plant/loopbacktransport.h
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
        # traceview::Transport (lib/core/transport.h).
        traceview_transport
    PRIVATE
        btp::catalog
)

# tracelab_control: the controller host and the ways to program a controller.
add_library(tracelab_control STATIC
    control/controlleradapter.h
    control/controllerhost.cpp
    control/controllerhost.h
    control/dllcontrolleradapter.cpp
    control/dllcontrolleradapter.h
    control/scriptcontrolleradapter.cpp
    control/scriptcontrolleradapter.h
)
target_include_directories(tracelab_control PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_control
    PUBLIC
        Qt6::Core
        # QJSEngine in scriptcontrolleradapter.h.
        Qt6::Qml
        traceview_transport
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
)

# tracelab_module: LabModule, what src/main.cpp registers.
add_library(tracelab_module STATIC
    module/labmodule.cpp
    module/labmodule.h
)
target_include_directories(tracelab_module PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_module
    PUBLIC
        traceview_modules
    PRIVATE
        Qt6::Widgets
        tracelab_scenes
        traceview_dashboard
)

# Filters: the portable C filter runtime and its host-side IIR design
# (lab/README.md, "Filters").
add_subdirectory(filters)

# Benchmarks: a plant, its controllers and a runner app per benchmark
# (lab/README.md, "Benchmarks").
add_subdirectory(benchmarks/linefollower)

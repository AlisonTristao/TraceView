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

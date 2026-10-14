# tracelab_lf_controllers: the line-follower controllers -- the qLPV MPC and
# the GPC, a C++ port of line_follower_simulator's mpc/ package behind
# lfcontroller.h, and a PD on the line sensors (pdcontroller.h).
# Plain C++17 and Eigen, no Qt, so it can later move into firmware.

# Eigen3::Eigen comes from lab/CMakeLists.txt (shared with tracelab_sensors).

add_library(tracelab_lf_controllers STATIC
    boxqp.h
    gpccontroller.cpp
    gpccontroller.h
    lfcontroller.cpp
    lfcontroller.h
    pdcontroller.cpp
    pdcontroller.h
    qlpvcontroller.cpp
    qlpvcontroller.h
    qlpvmodel.cpp
    qlpvmodel.h
    qlpvmpc.cpp
    qlpvmpc.h
    scheduling.cpp
    scheduling.h
)
set_target_properties(tracelab_lf_controllers PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(tracelab_lf_controllers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tracelab_lf_controllers PUBLIC cxx_std_17)
# The qLPV MPC runs on the GUI thread once per tick: unoptimized it takes
# over a second a step, so Debug builds still optimize it (and drop Eigen's
# asserts) to keep the simulation in real time.
if(NOT MSVC)
    target_compile_options(tracelab_lf_controllers PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
target_compile_definitions(tracelab_lf_controllers PRIVATE $<$<CONFIG:Debug>:EIGEN_NO_DEBUG>)
# PUBLIC: the internal headers (qlpvmpc.h, boxqp.h, ...) expose Eigen types,
# and the tests use them; lfcontroller.h itself does not include Eigen.
target_link_libraries(tracelab_lf_controllers PUBLIC Eigen3::Eigen tracelab_numeric)

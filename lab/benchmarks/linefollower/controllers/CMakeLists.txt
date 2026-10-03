# tracelab_lf_controllers: the line-follower controllers (qLPV MPC and GPC),
# a C++ port of line_follower_simulator's mpc/ package behind lfcontroller.h.
# Plain C++17 and Eigen, no Qt, so it can later move into firmware.

# Eigen3::Eigen comes from lab/CMakeLists.txt (shared with tracelab_sensors).

add_library(tracelab_lf_controllers STATIC
    boxqp.cpp
    boxqp.h
    gpccontroller.cpp
    gpccontroller.h
    lfcontroller.cpp
    lfcontroller.h
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
# PUBLIC: the internal headers (qlpvmpc.h, boxqp.h, ...) expose Eigen types,
# and the tests use them; lfcontroller.h itself does not include Eigen.
target_link_libraries(tracelab_lf_controllers PUBLIC Eigen3::Eigen)

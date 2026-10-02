# tracelab_lf_controllers: the line-follower controllers (qLPV MPC and GPC),
# a C++ port of line_follower_simulator's mpc/ package behind lfcontroller.h.
# Plain C++17 and Eigen, no Qt, so it can later move into firmware.

# Eigen 3.4 (MPL-2, header-only), pinned like BTP and hidapi in the root
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

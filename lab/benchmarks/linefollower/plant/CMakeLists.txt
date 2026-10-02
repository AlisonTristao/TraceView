# tracelab_lf_plant: the line follower's plant -- the track generator (same
# points as the Python simulator for the same seed), the car dynamics with
# its world pose, and the pygame Cluster logic (reach rule, coverage, future
# points) in geometry. Plain C++17, no Qt.
add_library(tracelab_lf_plant STATIC
    cardynamics.cpp
    cardynamics.h
    linefollowerplant.cpp
    linefollowerplant.h
    pyrandom.cpp
    pyrandom.h
    trackgenerator.cpp
    trackgenerator.h
    trackprogress.cpp
    trackprogress.h
)
set_target_properties(tracelab_lf_plant PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
# Includes rooted at the repository ("lab/benchmarks/linefollower/plant/...").
target_include_directories(tracelab_lf_plant PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tracelab_lf_plant PUBLIC cxx_std_17)

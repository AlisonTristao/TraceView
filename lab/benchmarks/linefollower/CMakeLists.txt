# Line-follower benchmark (lab/README.md, "Benchmarks"): the C++ port of
# line_follower_simulator -- the plant (tracelab_lf_plant), the controllers
# (tracelab_lf_controllers, when present) and the tracelab-linefollower
# runner app.

add_subdirectory(plant)

# The controllers are developed separately; until their folder exists the
# app is built without them (zero inputs, with a warning).
option(TRACELAB_LF_CONTROLLERS
    "Build the line-follower controllers (lab/benchmarks/linefollower/controllers) into tracelab-linefollower"
    ON)
if(TRACELAB_LF_CONTROLLERS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/controllers/CMakeLists.txt)
    add_subdirectory(controllers)
endif()

add_subdirectory(app)

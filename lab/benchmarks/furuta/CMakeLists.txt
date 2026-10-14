# Furuta pendulum benchmark (lab/README.md, "Benchmarks"): the plant and its
# controllers as code templates (tracelab_furuta), run by the generic
# TraceLab devices (tracelab.plant, tracelab.controller) and drawn by the
# "furuta" scene (lab/scenes/furutascene.h).
add_library(tracelab_furuta STATIC
    code/furutatemplates.cpp
    code/furutatemplates.h
)
target_include_directories(tracelab_furuta PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_furuta PUBLIC Qt6::Core tracelab_code)
qt_add_resources(tracelab_furuta tracelab_furuta_templates
    PREFIX "/tracelab/templates/furuta"
    BASE code
    FILES
        code/controllers/energy_lqr.c
        code/controllers/qlpv_mpc.c
        code/plants/pendulum.c
)

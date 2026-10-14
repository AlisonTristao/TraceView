# tracelab_lf_devices: the line follower as TraceView devices -- the plant
# (LinePlantDevice: the benchmark's plant on a fixed-step clock, a BTP
# producer of the loop profile), the controller host with the qLPV MPC / GPC
# adapter (LineControllerDevice), and the ModuleDeviceTypes that create them
# in process (lfdevices.h).
add_library(tracelab_lf_devices STATIC
    ../code/lftemplates.cpp
    ../code/lftemplates.h
    lfcontrolleradapter.cpp
    lfcontrolleradapter.h
    lfdevices.cpp
    lfdevices.h
    lfplantdevice.cpp
    lfplantdevice.h
    lfplantmanifest.cpp
    lfplantmanifest.h
    trackenvironment.cpp
    trackenvironment.h
)
target_include_directories(tracelab_lf_devices PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(tracelab_lf_devices
    PUBLIC
        Qt6::Core
        tracelab_plant
        tracelab_control
        tracelab_lf_plant
        traceview_transport
        btp::catalog
    PRIVATE
        tracelab_lf_controllers
        # The wheel-speed filter: the firmware's float runtime.
        tracelab_filters_rt
)

# The line follower's code templates (lftemplates.h): the controllers, filters
# and plant in C that the TraceLab tab opens and "back to the original"
# restores.
target_link_libraries(tracelab_lf_devices PUBLIC tracelab_code)
qt_add_resources(tracelab_lf_devices tracelab_lf_templates
    PREFIX "/tracelab/templates/linefollower"
    BASE ../code
    FILES
        ../code/controllers/gpc.c
        ../code/controllers/pd.c
        ../code/controllers/qlpv_mpc.c
        ../code/filters/kalman.c
        ../code/filters/lowpass.c
        ../code/plants/car.c
)
# The header a plant on the track includes: in the folder every compile
# reads (lab/code's runtime), next to tracelab.h.
qt_add_resources(tracelab_lf_devices tracelab_lf_track_header
    PREFIX "/tracelab/code/runtime"
    BASE ../code/include
    FILES ../code/include/tracelab_track.h
)

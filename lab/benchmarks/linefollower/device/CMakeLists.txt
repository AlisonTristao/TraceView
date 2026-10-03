# tracelab_lf_devices: the line follower as TraceView devices -- the plant
# (LinePlantDevice: the benchmark's plant on a fixed-step clock, a BTP
# producer of the loop profile), the controller host with the qLPV MPC / GPC
# adapter (LineControllerDevice), and the ModuleDeviceTypes that create them
# in process (lfdevices.h).
add_library(tracelab_lf_devices STATIC
    lfcontrolleradapter.cpp
    lfcontrolleradapter.h
    lfdevices.cpp
    lfdevices.h
    lfplantdevice.cpp
    lfplantdevice.h
    lfplantmanifest.cpp
    lfplantmanifest.h
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

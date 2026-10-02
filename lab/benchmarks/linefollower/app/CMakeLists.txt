# tracelab-linefollower: the line-follower benchmark in one window (plant,
# controller, scene), a provisional in-process harness until the plant and
# the controller host speak BTP (see simwindow.h).
add_executable(tracelab-linefollower
    lfrunner.cpp
    lfrunner.h
    main.cpp
    simwindow.cpp
    simwindow.h
)
target_link_libraries(tracelab-linefollower PRIVATE
    Qt6::Widgets
    tracelab_lf_plant
    tracelab_scenes
)
if(TARGET tracelab_lf_controllers)
    target_link_libraries(tracelab-linefollower PRIVATE tracelab_lf_controllers)
else()
    # Zero inputs and a warning at startup.
    target_compile_definitions(tracelab-linefollower PRIVATE TRACELAB_LF_NO_CONTROLLERS)
endif()

if(WIN32)
    # Its own folder with the Qt DLLs next to it (see tools/CMakeLists.txt):
    # otherwise the DLL search can pick an ABI-incompatible Qt from PATH.
    set_target_properties(tracelab-linefollower PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lab/tracelab-linefollower"
    )
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET tracelab-linefollower POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:tracelab-linefollower>"
            COMMENT "Deploying Qt runtime DLLs next to tracelab-linefollower.exe"
        )
    endif()
endif()

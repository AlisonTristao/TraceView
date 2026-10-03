# Filters (lab/README.md, "Filters"): a filter chain that runs in portable C,
# so the same code can be exported to the robot firmware, and the host-side
# design that computes its IIR coefficients.
#
#   tracelab_filters_rt      runtime/tlfilter.{h,c}: C99, no allocation, float
#                            (or double with TRACELAB_FILTERS_DOUBLE)
#   tracelab_filters_rt_f64  the same source always in double, for host-side
#                            analysis and for the bit-tight tests against Iir1
#   tracelab_filters_design  design/tlfilterdesign.{h,cpp}: C++17, Iir1
#
# Includes are rooted at the repository ("lab/filters/runtime/tlfilter.h").

enable_language(C)

option(TRACELAB_FILTERS_DOUBLE "Build tracelab_filters_rt with tlf_real = double (TLF_USE_DOUBLE)" OFF)

function(tracelab_add_filters_rt name)
    add_library(${name} STATIC runtime/tlfilter.c runtime/tlfilter.h)
    set_target_properties(${name} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -pedantic)
        # libm, where the platform keeps it apart from libc.
        if(NOT WIN32)
            target_link_libraries(${name} PUBLIC m)
        endif()
    endif()
endfunction()

tracelab_add_filters_rt(tracelab_filters_rt)
if(TRACELAB_FILTERS_DOUBLE)
    target_compile_definitions(tracelab_filters_rt PUBLIC TLF_USE_DOUBLE)
endif()

tracelab_add_filters_rt(tracelab_filters_rt_f64)
target_compile_definitions(tracelab_filters_rt_f64 PUBLIC TLF_USE_DOUBLE)

# Iir1 (https://github.com/berndporr/iir1, MIT), pinned. Only its sources are
# used: SOURCE_SUBDIR points at a folder that does not exist, so its own
# CMakeLists (a shared and a static library, demos, tests and install rules
# that would end up in TraceView's packages) is never added, and the static
# library below is built from its iir/*.cpp instead.
include(FetchContent)
FetchContent_Declare(
    iir1
    GIT_REPOSITORY https://github.com/berndporr/iir1.git
    GIT_TAG 1.9.5
    SOURCE_SUBDIR do-not-add-iir1-cmakelists
)
FetchContent_MakeAvailable(iir1)

add_library(tracelab_iir1 STATIC
    ${iir1_SOURCE_DIR}/iir/Biquad.cpp
    ${iir1_SOURCE_DIR}/iir/Butterworth.cpp
    ${iir1_SOURCE_DIR}/iir/Cascade.cpp
    ${iir1_SOURCE_DIR}/iir/ChebyshevI.cpp
    ${iir1_SOURCE_DIR}/iir/ChebyshevII.cpp
    ${iir1_SOURCE_DIR}/iir/Custom.cpp
    ${iir1_SOURCE_DIR}/iir/PoleFilter.cpp
    ${iir1_SOURCE_DIR}/iir/RBJ.cpp
)
set_target_properties(tracelab_iir1 PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
# SYSTEM: Iir1's headers are not held to this project's warnings.
target_include_directories(tracelab_iir1 SYSTEM PUBLIC ${iir1_SOURCE_DIR})
target_include_directories(tracelab_iir1 PRIVATE ${iir1_SOURCE_DIR}/iir)
target_compile_features(tracelab_iir1 PUBLIC cxx_std_11)

add_library(tracelab_filters_design STATIC
    design/tlfilterdesign.cpp
    design/tlfilterdesign.h
)
set_target_properties(tracelab_filters_design PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(tracelab_filters_design PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tracelab_filters_design PUBLIC cxx_std_17)
# tlfilterdesign.h includes tlfilter.h for toRuntime() (header-inline, so it
# takes the caller's tlf_real): the design needs the runtime's header, not one
# of its two libraries; the consumer links the one it wants.
target_link_libraries(tracelab_filters_design PRIVATE tracelab_iir1)

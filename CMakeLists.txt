cmake_minimum_required(VERSION 3.21)

project(TraceView
    VERSION 0.1.0
    DESCRIPTION "Telemetry dashboard for line-following robots"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)

add_subdirectory(lib)

qt_add_executable(TraceView
    src/main.cpp
)

target_include_directories(TraceView PRIVATE include)

target_link_libraries(TraceView PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    traceview_core
)

set_target_properties(TraceView PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

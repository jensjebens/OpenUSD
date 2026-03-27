cmake_minimum_required(VERSION 3.14)
project(execMetricsUnits)

if(DEFINED PXR_DIR AND NOT DEFINED pxr_DIR)
    set(pxr_DIR "${PXR_DIR}")
endif()

find_package(pxr REQUIRED)

add_library(execMetricsUnits SHARED
    execMetricsUnits.cpp
)

target_include_directories(execMetricsUnits
    PRIVATE
        ${PXR_INCLUDE_DIRS}
)

target_link_libraries(execMetricsUnits
    PRIVATE
        exec
        execUsd
        plug
        sdf
        tf
        usd
        usdGeom
        vdf
        gf
        vt
)

set_target_properties(execMetricsUnits PROPERTIES
    PREFIX "lib"
)

# Copy resources next to the library for plugin discovery
add_custom_command(TARGET execMetricsUnits POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        ${CMAKE_BINARY_DIR}/resources
)

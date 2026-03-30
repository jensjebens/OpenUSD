cmake_minimum_required(VERSION 3.14)
project(execDsoOwnership)

if(DEFINED PXR_DIR AND NOT DEFINED pxr_DIR)
    set(pxr_DIR "${PXR_DIR}")
endif()

find_package(pxr REQUIRED)

# --- DSO computation shared library ---
add_library(execDsoOwnership SHARED
    execDsoOwnership.cpp
)

target_include_directories(execDsoOwnership
    PRIVATE
        ${PXR_INCLUDE_DIRS}
)

target_link_libraries(execDsoOwnership
    PRIVATE
        exec
        execUsd
        execGeom
        plug
        sdf
        tf
        usd
        usdGeom
        vdf
        gf
        vt
)

set_target_properties(execDsoOwnership PROPERTIES
    PREFIX "lib"
)

# Copy resources next to the library for plugin discovery
add_custom_command(TARGET execDsoOwnership POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        ${CMAKE_BINARY_DIR}/resources
)

# --- Standalone computation test ---
add_executable(testDsoComputation
    testenv/testDsoComputation.cpp
    execDsoOwnership.cpp
)

target_include_directories(testDsoComputation
    PRIVATE
        ${PXR_INCLUDE_DIRS}
)

target_link_libraries(testDsoComputation
    PRIVATE
        exec
        execUsd
        execGeom
        plug
        sdf
        tf
        usd
        usdGeom
        vdf
        gf
        vt
)

add_custom_command(TARGET testDsoComputation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        ${CMAKE_CURRENT_BINARY_DIR}/resources
)

# --- HdExec integration test ---
add_executable(testDsoHdExec
    testenv/testDsoHdExec.cpp
    execDsoOwnership.cpp
)

target_include_directories(testDsoHdExec
    PRIVATE
        ${PXR_INCLUDE_DIRS}
)

target_link_libraries(testDsoHdExec
    PRIVATE
        exec
        execUsd
        execGeom
        hdExec
        hd
        plug
        sdf
        tf
        usd
        usdGeom
        usdImaging
        vdf
        gf
        vt
)

add_custom_command(TARGET testDsoHdExec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        ${CMAKE_CURRENT_BINARY_DIR}/resources
)

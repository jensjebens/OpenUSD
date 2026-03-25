#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

# FindNewtonDynamics.cmake
#
# Finds Newton Dynamics 4 headers and libraries.
#
# This module defines:
#   NewtonDynamics_FOUND          - True if Newton Dynamics was found
#   NEWTON_DYNAMICS_INCLUDE_DIRS  - Include directories
#   NEWTON_DYNAMICS_LIBRARIES     - Libraries to link against
#
# Users may set:
#   NEWTON_DYNAMICS_ROOT          - Root directory of Newton Dynamics install
#
# If not found on the system, falls back to FetchContent to download
# Newton 4 from GitHub.

include(FindPackageHandleStandardArgs)

# Try to find Newton headers
find_path(NEWTON_DYNAMICS_INCLUDE_DIR
    NAMES ndNewton.h
    PATHS
        ${NEWTON_DYNAMICS_ROOT}/include
        ${NEWTON_DYNAMICS_ROOT}/sdk/dNewton
        /usr/local/include/newton
        /usr/include/newton
    PATH_SUFFIXES
        newton-4.00/sdk/dNewton
        newton
)

# Try to find Newton libraries
find_library(NEWTON_DYNAMICS_CORE_LIB
    NAMES ndCore dCore
    PATHS
        ${NEWTON_DYNAMICS_ROOT}/lib
        /usr/local/lib
        /usr/lib
    PATH_SUFFIXES
        newton-4.00
        newton
)

find_library(NEWTON_DYNAMICS_COLLISION_LIB
    NAMES ndCollision dCollision
    PATHS
        ${NEWTON_DYNAMICS_ROOT}/lib
        /usr/local/lib
        /usr/lib
    PATH_SUFFIXES
        newton-4.00
        newton
)

find_library(NEWTON_DYNAMICS_NEWTON_LIB
    NAMES ndNewton dNewton
    PATHS
        ${NEWTON_DYNAMICS_ROOT}/lib
        /usr/local/lib
        /usr/lib
    PATH_SUFFIXES
        newton-4.00
        newton
)

find_package_handle_standard_args(NewtonDynamics
    REQUIRED_VARS
        NEWTON_DYNAMICS_INCLUDE_DIR
        NEWTON_DYNAMICS_CORE_LIB
        NEWTON_DYNAMICS_COLLISION_LIB
        NEWTON_DYNAMICS_NEWTON_LIB
)

if(NewtonDynamics_FOUND)
    set(NEWTON_DYNAMICS_INCLUDE_DIRS ${NEWTON_DYNAMICS_INCLUDE_DIR})
    set(NEWTON_DYNAMICS_LIBRARIES
        ${NEWTON_DYNAMICS_NEWTON_LIB}
        ${NEWTON_DYNAMICS_COLLISION_LIB}
        ${NEWTON_DYNAMICS_CORE_LIB}
    )
    mark_as_advanced(
        NEWTON_DYNAMICS_INCLUDE_DIR
        NEWTON_DYNAMICS_CORE_LIB
        NEWTON_DYNAMICS_COLLISION_LIB
        NEWTON_DYNAMICS_NEWTON_LIB
    )

    add_definitions(-DNEWTON_DYNAMICS_FOUND)
else()
    # FetchContent fallback — attempt to pull Newton 4 from GitHub.
    # This is optional and only triggered when explicitly requested
    # via -DNEWTON_FETCH_CONTENT=ON.
    if(NEWTON_FETCH_CONTENT)
        include(FetchContent)
        FetchContent_Declare(
            newton_dynamics
            GIT_REPOSITORY https://github.com/MADEAPPS/newton-dynamics.git
            GIT_TAG        master
            GIT_SHALLOW    TRUE
        )

        set(NEWTON_BUILD_SANDBOX_DEMOS OFF CACHE BOOL "" FORCE)
        set(NEWTON_BUILD_PROFILER OFF CACHE BOOL "" FORCE)
        set(NEWTON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(newton_dynamics)

        set(NEWTON_DYNAMICS_INCLUDE_DIRS
            ${newton_dynamics_SOURCE_DIR}/newton-4.00/sdk/dCore
            ${newton_dynamics_SOURCE_DIR}/newton-4.00/sdk/dCollision
            ${newton_dynamics_SOURCE_DIR}/newton-4.00/sdk/dNewton
        )
        set(NEWTON_DYNAMICS_LIBRARIES ndNewton ndCollision ndCore)
        set(NewtonDynamics_FOUND TRUE)

        add_definitions(-DNEWTON_DYNAMICS_FOUND)
    endif()
endif()

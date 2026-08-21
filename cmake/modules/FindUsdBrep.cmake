# Locate usd-brep, NVIDIA's NURBS B-rep solid modelling kernel.
#
# The kernel is being renamed from SMLib to usd-brep (OMPE-105005). This module
# is named for the destination; the artefacts it looks for still carry the
# SMLib names until that rename lands in the source tree.
#
# usd-brep builds with premake into a source tree rather than installing into a
# prefix, so point this at a built checkout:
#
#   -DUSDBREP_ROOT=/path/to/solidmodeling
#
# Sets:
#   UsdBrep_FOUND
#   UsdBrep_INCLUDE_DIRS
#   UsdBrep_LIBRARIES
#
# Note on the USD-facing libraries: BREP_SM_USD and BREP_USD_DATA take pxr types
# in their signatures, so they must have been built against the same OpenUSD
# this project builds. usd-brep's deps/target-deps.packman.xml pins a packman
# USD by default; building against a different one requires overriding that
# dependency. A mismatch produces link errors naming pxrInternal_v0_<X>_<Y>
# symbols for the version you are not building.

if (NOT USDBREP_ROOT)
    set(USDBREP_ROOT $ENV{USDBREP_ROOT})
endif()

if (APPLE)
    set(_USDBREP_PLATFORM "macos-universal")
elseif (WIN32)
    set(_USDBREP_PLATFORM "windows-x86_64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_USDBREP_PLATFORM "linux-aarch64")
else()
    set(_USDBREP_PLATFORM "linux-x86_64")
endif()

set(_USDBREP_LIBDIR "${USDBREP_ROOT}/_build/${_USDBREP_PLATFORM}/release")

find_path(UsdBrep_SM_API_INCLUDE_DIR SmApiBrep.h
    PATHS "${USDBREP_ROOT}/source/SM_API/inc" NO_DEFAULT_PATH)
find_path(UsdBrep_CORE_INCLUDE_DIR SmBrep.h
    PATHS "${USDBREP_ROOT}/source/SMLib/inc" NO_DEFAULT_PATH)
find_path(UsdBrep_BREP_SM_USD_INCLUDE_DIR SmuConvert.h
    PATHS "${USDBREP_ROOT}/source/BREP_SM_USD/inc" NO_DEFAULT_PATH)
find_path(UsdBrep_BREP_USD_DATA_INCLUDE_DIR UsdBrepRead.h
    PATHS "${USDBREP_ROOT}/source/BREP_USD_DATA/inc" NO_DEFAULT_PATH)

foreach(_lib SMLib SM_API BREP_SM_USD BREP_USD_DATA)
    find_library(UsdBrep_${_lib}_LIBRARY ${_lib}
        PATHS "${_USDBREP_LIBDIR}" NO_DEFAULT_PATH)
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UsdBrep
    REQUIRED_VARS
        UsdBrep_SM_API_INCLUDE_DIR
        UsdBrep_CORE_INCLUDE_DIR
        UsdBrep_BREP_SM_USD_INCLUDE_DIR
        UsdBrep_BREP_USD_DATA_INCLUDE_DIR
        UsdBrep_SMLib_LIBRARY
        UsdBrep_SM_API_LIBRARY
        UsdBrep_BREP_SM_USD_LIBRARY
        UsdBrep_BREP_USD_DATA_LIBRARY
    FAIL_MESSAGE
        "usd-brep not found. Set USDBREP_ROOT to a built solidmodeling checkout.")

if (UsdBrep_FOUND)
    set(UsdBrep_INCLUDE_DIRS
        ${UsdBrep_SM_API_INCLUDE_DIR}
        ${UsdBrep_CORE_INCLUDE_DIR}
        ${UsdBrep_BREP_SM_USD_INCLUDE_DIR}
        ${UsdBrep_BREP_USD_DATA_INCLUDE_DIR})
    set(UsdBrep_LIBRARIES
        ${UsdBrep_BREP_SM_USD_LIBRARY}
        ${UsdBrep_BREP_USD_DATA_LIBRARY}
        ${UsdBrep_SM_API_LIBRARY}
        ${UsdBrep_SMLib_LIBRARY})
endif()

mark_as_advanced(
    UsdBrep_SM_API_INCLUDE_DIR UsdBrep_CORE_INCLUDE_DIR
    UsdBrep_BREP_SM_USD_INCLUDE_DIR UsdBrep_BREP_USD_DATA_INCLUDE_DIR
    UsdBrep_SMLib_LIBRARY UsdBrep_SM_API_LIBRARY
    UsdBrep_BREP_SM_USD_LIBRARY UsdBrep_BREP_USD_DATA_LIBRARY)

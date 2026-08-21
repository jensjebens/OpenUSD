# Locate SMLib, NVIDIA's NURBS B-rep solid modelling kernel, published as
# usd-brep.
#
# SMLib builds with premake into a source tree rather than installing into a
# prefix, so this module is pointed at a built checkout:
#
#   -DSMLIB_ROOT=/path/to/solidmodeling
#
# Sets:
#   SMLib_FOUND
#   SMLib_INCLUDE_DIRS
#   SMLib_LIBRARIES
#
# Note on the USD-facing libraries: BREP_SM_USD and BREP_USD_DATA take pxr
# types in their signatures, so they must have been built against the same
# OpenUSD this project builds. SMLib's deps/target-deps.packman.xml pins a
# packman USD by default; building against a different one requires overriding
# that dependency. A mismatch produces link errors naming pxrInternal_v0_<X>_<Y>
# symbols for the version you are not building.

if (NOT SMLIB_ROOT)
    set(SMLIB_ROOT $ENV{SMLIB_ROOT})
endif()

if (APPLE)
    set(_SMLIB_PLATFORM "macos-universal")
elseif (WIN32)
    set(_SMLIB_PLATFORM "windows-x86_64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_SMLIB_PLATFORM "linux-aarch64")
else()
    set(_SMLIB_PLATFORM "linux-x86_64")
endif()

set(_SMLIB_LIBDIR "${SMLIB_ROOT}/_build/${_SMLIB_PLATFORM}/release")

find_path(SMLib_SM_API_INCLUDE_DIR SmApiBrep.h
    PATHS "${SMLIB_ROOT}/source/SM_API/inc" NO_DEFAULT_PATH)
find_path(SMLib_CORE_INCLUDE_DIR SmBrep.h
    PATHS "${SMLIB_ROOT}/source/SMLib/inc" NO_DEFAULT_PATH)
find_path(SMLib_BREP_SM_USD_INCLUDE_DIR SmuConvert.h
    PATHS "${SMLIB_ROOT}/source/BREP_SM_USD/inc" NO_DEFAULT_PATH)
find_path(SMLib_BREP_USD_DATA_INCLUDE_DIR UsdBrepRead.h
    PATHS "${SMLIB_ROOT}/source/BREP_USD_DATA/inc" NO_DEFAULT_PATH)

foreach(_lib SMLib SM_API BREP_SM_USD BREP_USD_DATA)
    find_library(SMLib_${_lib}_LIBRARY ${_lib}
        PATHS "${_SMLIB_LIBDIR}" NO_DEFAULT_PATH)
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SMLib
    REQUIRED_VARS
        SMLib_SM_API_INCLUDE_DIR
        SMLib_CORE_INCLUDE_DIR
        SMLib_BREP_SM_USD_INCLUDE_DIR
        SMLib_BREP_USD_DATA_INCLUDE_DIR
        SMLib_SMLib_LIBRARY
        SMLib_SM_API_LIBRARY
        SMLib_BREP_SM_USD_LIBRARY
        SMLib_BREP_USD_DATA_LIBRARY
    FAIL_MESSAGE
        "SMLib not found. Set SMLIB_ROOT to a built solidmodeling checkout.")

if (SMLib_FOUND)
    set(SMLib_INCLUDE_DIRS
        ${SMLib_SM_API_INCLUDE_DIR}
        ${SMLib_CORE_INCLUDE_DIR}
        ${SMLib_BREP_SM_USD_INCLUDE_DIR}
        ${SMLib_BREP_USD_DATA_INCLUDE_DIR})
    set(SMLib_LIBRARIES
        ${SMLib_BREP_SM_USD_LIBRARY}
        ${SMLib_BREP_USD_DATA_LIBRARY}
        ${SMLib_SM_API_LIBRARY}
        ${SMLib_SMLib_LIBRARY})
endif()

mark_as_advanced(
    SMLib_SM_API_INCLUDE_DIR SMLib_CORE_INCLUDE_DIR
    SMLib_BREP_SM_USD_INCLUDE_DIR SMLib_BREP_USD_DATA_INCLUDE_DIR
    SMLib_SMLib_LIBRARY SMLib_SM_API_LIBRARY
    SMLib_BREP_SM_USD_LIBRARY SMLib_BREP_USD_DATA_LIBRARY)

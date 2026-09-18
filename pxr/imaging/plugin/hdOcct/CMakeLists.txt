set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdOcct)

if(NOT OpenCASCADE_FOUND AND NOT OCCT_INCLUDE_DIR)
    message(WARNING "Not building ${PXR_PACKAGE} because of missing dependency: OpenCASCADE")
    return()
endif()

# LGPL-2.1 compliance: OCCT must be dynamically linked.
# This plugin does NOT support static linking of OCCT libraries.
# See NOTICE.md for full LGPL compliance documentation.
if (DEFINED OpenCASCADE_LIBRARIES)
    foreach(_lib ${OpenCASCADE_LIBRARIES})
        if (WIN32)
            # On Windows, .lib files are expected (import libraries for DLLs).
            # Static libraries are also .lib but lack a corresponding .dll.
            # We can't easily distinguish at configure time, so trust that
            # find_package(OpenCASCADE) found the shared build. The official
            # OCCT CMake config and vcpkg both default to shared libraries.
        else()
            if (_lib MATCHES "\\.(a)$")
                message(FATAL_ERROR
                    "hdOcct: Static OCCT library detected (${_lib}). "
                    "LGPL-2.1 compliance requires dynamic linking only. "
                    "Please install OCCT shared libraries.")
            endif()
        endif()
    endforeach()
endif()

# Determine OCCT include and library paths
if (OpenCASCADE_FOUND AND DEFINED OpenCASCADE_INCLUDE_DIR)
    set(_OCCT_INCLUDE_DIR ${OpenCASCADE_INCLUDE_DIR})

    # Link only the OCCT modules this plugin uses: FoundationClasses
    # (TKernel, TKMath), ModelingData (TKG2d, TKG3d, TKGeomBase, TKBRep) and
    # ModelingAlgorithms (TKTopAlgo, TKMesh, TKShHealing and friends).
    #
    # OpenCASCADE_LIBRARIES is the union of every module OCCT was built with,
    # including Visualization, ApplicationFramework and Draw. Distribution
    # packages commonly split those into separate -dev packages, so linking the
    # union makes the plugin fail to link against a stock install with errors
    # like "cannot find -lTKDraw". Draw is OCCT's Tcl test harness and has no
    # business being a dependency of a Hydra plugin.
    if (DEFINED OpenCASCADE_ModelingAlgorithms_LIBRARIES)
        set(_OCCT_LIBRARIES
            ${OpenCASCADE_FoundationClasses_LIBRARIES}
            ${OpenCASCADE_ModelingData_LIBRARIES}
            ${OpenCASCADE_ModelingAlgorithms_LIBRARIES})
    else()
        # Older OCCT configs do not publish the per-module lists.
        set(_OCCT_LIBRARIES ${OpenCASCADE_LIBRARIES})
    endif()
else()
    # Fallback: use the variables set by Packages.cmake
    set(_OCCT_INCLUDE_DIR ${OCCT_INCLUDE_DIR})
    set(_OCCT_LIBRARIES ${OCCT_LIBRARIES})
endif()

pxr_plugin(${PXR_PACKAGE}
    LIBRARIES
        tf
        gf
        vt
        sdf
        usd
        usdGeom
        hd
        hdGp
        hdsi
        usdImaging
        ${_OCCT_LIBRARIES}

    INCLUDE_DIRS
        ${_OCCT_INCLUDE_DIR}

    PUBLIC_CLASSES
        tessellator
        brepBuilder
        meshExporter
        hydraGenerativePlugin
        brepArrayAdapter

    CPPFILES
        meshExportC.cpp

    PUBLIC_HEADERS
        api.h

    RESOURCE_FILES
        plugInfo.json

    DISABLE_PRECOMPILED_HEADERS
)

pxr_test_scripts(
    testenv/testUsdSolidTessellation.py
)

pxr_install_test_dir(
    SRC testenv/testUsdSolidTessellation
    DEST testUsdSolidTessellation
)

pxr_register_test(testUsdSolidTessellation
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdSolidTessellation"
    ENV
        USDSOLIDTESSELLATE_BIN=${CMAKE_INSTALL_PREFIX}/bin/usdsolidtessellate
    EXPECTED_RETURN_CODE 0
)

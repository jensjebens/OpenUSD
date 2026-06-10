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
        if (_lib MATCHES "\\.(a|lib)$" AND NOT _lib MATCHES "import")
            message(FATAL_ERROR
                "hdOcct: Static OCCT library detected (${_lib}). "
                "LGPL-2.1 compliance requires dynamic linking only. "
                "Please install OCCT shared libraries.")
        endif()
    endforeach()
endif()

# Determine OCCT include and library paths
if (OpenCASCADE_FOUND AND DEFINED OpenCASCADE_INCLUDE_DIR)
    set(_OCCT_INCLUDE_DIR ${OpenCASCADE_INCLUDE_DIR})
    set(_OCCT_LIBRARIES ${OpenCASCADE_LIBRARIES})
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

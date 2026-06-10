# USD Solid Tessellator — in-tree extras plugin
#
# Tessellates UsdSolid BrepArray (Proposal #109) into UsdGeomMesh
# using OpenCascade (OCCT) and provides a Hydra Generative Procedural
# for live BrepArray visualization.

set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdSolidTessellator)

# --- Find OpenCascade ---
# OCCT is an external dependency not part of the USD build.
# Users must set OCCT_ROOT or have it discoverable.
find_package(OpenCASCADE QUIET)
if(NOT OpenCASCADE_FOUND)
    # Fallback: try Ubuntu system paths
    set(OCCT_INCLUDE_DIR "/usr/include/opencascade" CACHE PATH "OCCT headers")
    if(EXISTS "${OCCT_INCLUDE_DIR}/BRep_Builder.hxx")
        set(OpenCASCADE_FOUND TRUE)
        message(STATUS "usdSolidTessellator: Using system OCCT at ${OCCT_INCLUDE_DIR}")
    else()
        message(WARNING "usdSolidTessellator: OpenCASCADE not found — skipping build")
        return()
    endif()
else()
    set(OCCT_INCLUDE_DIR ${OpenCASCADE_INCLUDE_DIR})
endif()

set(OCCT_LIBRARIES
    TKernel
    TKMath
    TKG2d
    TKG3d
    TKGeomBase
    TKBRep
    TKGeomAlgo
    TKTopAlgo
    TKMesh
    TKShHealing
)

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
        hf
        ${OCCT_LIBRARIES}

    INCLUDE_DIRS
        ${OCCT_INCLUDE_DIR}

    PUBLIC_CLASSES
        tessellator
        brepBuilder
        meshExporter
        hydraGenerativePlugin

    PUBLIC_HEADERS
        api.h

    RESOURCE_FILES
        plugInfo.json

    DISABLE_PRECOMPILED_HEADERS
)

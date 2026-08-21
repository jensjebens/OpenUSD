set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdUsdBrep)

if (NOT UsdBrep_FOUND)
    message(WARNING "Not building ${PXR_PACKAGE} because of missing dependency: usd-brep")
    return()
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
        ${UsdBrep_LIBRARIES}

    INCLUDE_DIRS
        ${UsdBrep_INCLUDE_DIRS}

    PUBLIC_CLASSES
        tessellator

    PUBLIC_HEADERS
        api.h

    DISABLE_PRECOMPILED_HEADERS
)

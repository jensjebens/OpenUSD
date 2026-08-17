set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdSmlib)

if (NOT SMLib_FOUND)
    message(WARNING "Not building ${PXR_PACKAGE} because of missing dependency: SMLib")
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
        ${SMLib_LIBRARIES}

    INCLUDE_DIRS
        ${SMLib_INCLUDE_DIRS}

    PUBLIC_CLASSES
        tessellator

    PUBLIC_HEADERS
        api.h

    DISABLE_PRECOMPILED_HEADERS
)

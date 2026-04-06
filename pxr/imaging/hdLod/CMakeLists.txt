set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdLod)

pxr_library(hdLod
    LIBRARIES
        hd
        usd
        sdf
        tf
        gf
        vt

    PUBLIC_CLASSES
        hdLodSceneIndex
        sceneIndexPlugin

    PUBLIC_HEADERS
        api.h

    RESOURCE_FILES
        plugInfo.json
)

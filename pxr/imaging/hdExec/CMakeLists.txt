set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdExec)

pxr_library(hdExec
    LIBRARIES
        gf
        hd
        plug
        sdf
        tf
        usd
        usdImaging
        vt
        exec
        execUsd

    PUBLIC_HEADERS
        api.h

    PUBLIC_CLASSES
        execComputedTransformSceneIndex
        physicsXformProvider
        sceneIndexPlugin

    PYMODULE_CPPFILES
        module.cpp
        wrapExecComputedTransformSceneIndex.cpp

    PRIVATE_CLASSES

    CPPFILES
        usdImagingSceneIndexPlugin.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        README.md
)

# Test plugins and the tests that rely on them aren't currently supported in
# static builds.
if (BUILD_SHARED_LIBS)
    pxr_build_test(testHdExecComputedTransformSceneIndex
        LIBRARIES
            gf
            hd
            hdExec
            exec
            execGeom
            execUsd
            plug
            sdf
            tf
            usd
            usdGeom
            vt
            work
        CPPFILES
            testenv/testHdExecComputedTransformSceneIndex.cpp
    )

    pxr_register_test(testHdExecComputedTransformSceneIndex
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdExecComputedTransformSceneIndex"
        TESTENV testenv/testHdExecComputedTransformSceneIndex
        EXPECTED_RETURN_CODE 0
    )
endif()

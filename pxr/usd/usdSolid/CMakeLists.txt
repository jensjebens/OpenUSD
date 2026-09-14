set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdSolid)

pxr_library(usdSolid
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        plug
        vt
        sdf
        usd
        usdGeom

    PUBLIC_HEADERS
        api.h

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
        generatedSchema.usda
        schema.usda

    DISABLE_PRECOMPILED_HEADERS
)

pxr_test_scripts(
    testenv/testUsdSolidBrepArray.py
)

pxr_register_test(testUsdSolidBrepArray
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdSolidBrepArray"
    EXPECTED_RETURN_CODE 0
)

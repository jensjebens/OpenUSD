set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdProfiles)

pxr_library(usdProfiles
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        plug
        vt
        sdf
        usd
        work

    PUBLIC_CLASSES
        capabilityRegistry
        profileAPI
        profileRegistry
        queries
        tokens

    PUBLIC_HEADERS
        api.h

    PYMODULE_CPPFILES
        module.cpp
        wrapCapabilityRegistry.cpp
        wrapProfileAPI.cpp
        wrapProfileRegistry.cpp
        wrapQueries.cpp
        wrapTokens.cpp

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
        overview.dox
)

pxr_test_scripts(
    testenv/testUsdProfilesBasic.py
)

pxr_register_test(testUsdProfilesBasic
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdProfilesBasic"
    EXPECTED_RETURN_CODE 0
)

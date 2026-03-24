set(PXR_PACKAGE exec)

pxr_build_test(unitsResolution
    LIBRARIES
        exec
        execUsd
        plug
        sdf
        tf
        usd
        usdGeom
        vdf
    CPPFILES
        unitsResolution.cpp
)

pxr_build_test(testUnitsResolution
    LIBRARIES
        exec
        execUsd
        plug
        sdf
        tf
        usd
        vdf
        vt
    CPPFILES
        testUnitsResolution.cpp
)

pxr_register_test(testUnitsResolutionExample
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUnitsResolution"
    TESTENV testenv/testUnitsResolution
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testUnitsResolutionPerf
    LIBRARIES
        exec
        execUsd
        gf
        plug
        sdf
        tf
        usd
        usdGeom
        vdf
        vt
    CPPFILES
        testUnitsResolutionPerf.cpp
)

pxr_register_test(testUnitsResolutionPerfExample
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUnitsResolutionPerf"
    TESTENV testenv/testUnitsResolution
    EXPECTED_RETURN_CODE 0
)

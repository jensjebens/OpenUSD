pxr_python_bin(stepToUsdSolid
    DEPENDENCIES
        tf
        gf
        vt
        sdf
        usd
        usdGeom
        usdSolid
)

# The test converts a STEP file and runs the usdSolid validators over the
# result, so it only means anything when the validation library is built.
if (PXR_BUILD_USD_VALIDATION)
    # A registered test runs from a temporary directory with
    # ${CMAKE_INSTALL_PREFIX}/tests as the only entry Python puts on sys.path,
    # so the converter has to be installed there too for the test's
    # "import stepToUsdSolid" to resolve. pxr/usd/usd installs
    # testUsdNamespaceEditorDependentEditsBase.py the same way.
    pxr_test_scripts(
        testenv/testStepToUsdSolid.py
        stepToUsdSolid.py:stepToUsdSolid.py
    )

    pxr_register_test(testStepToUsdSolid
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testStepToUsdSolid"
        EXPECTED_RETURN_CODE 0
    )
endif()

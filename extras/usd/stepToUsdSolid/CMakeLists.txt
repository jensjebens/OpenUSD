if (NOT PXR_ENABLE_PYTHON_SUPPORT)
    message(STATUS "Not installing stepToUsdSolid because "
                   "PXR_ENABLE_PYTHON_SUPPORT=OFF")
    return()
endif()

set(PXR_PREFIX stepToUsdSolid)
set(PXR_INSTALL_SUBDIR share/usd/stepToUsdSolid)

# pxr_test_scripts and pxr_register_test register nothing when PXR_PACKAGE
# names no build target. The sample authors its output through the usdSolid
# schema, so key its test to that library.
set(PXR_PACKAGE usdSolid)

install(
    FILES
        stepToUsdSolid.py
    DESTINATION
        ${PXR_INSTALL_SUBDIR}
)

# The test converts a STEP file and then runs the usdSolid validators over the
# result, so it needs the validation library.
if (PXR_BUILD_USD_VALIDATION)
    # A registered test runs out of a temporary directory with the installed
    # copy of the script as its argument, which leaves ${CMAKE_INSTALL_PREFIX}/
    # tests as the only directory Python puts on sys.path. Install the
    # converter there as well, keeping its .py suffix, so that the test's
    # "import stepToUsdSolid" resolves. pxr/usd/usd installs
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
else()
    message(STATUS "Not registering testStepToUsdSolid because "
                   "PXR_BUILD_USD_VALIDATION=OFF")
endif()

set(PXR_PACKAGE exec)

# Optionally find Newton Dynamics 4 for full physics simulation.
# The plugin skeleton compiles without it (stubs only).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(NewtonDynamics QUIET)

set(NEWTON_PHYSICS_LIBS
    exec
    execUsd
    gf
    plug
    sdf
    tf
    usd
    usdGeom
    usdPhysics
    vdf
    vt
)

if(NewtonDynamics_FOUND)
    list(APPEND NEWTON_PHYSICS_LIBS ${NEWTON_DYNAMICS_LIBRARIES})
endif()

pxr_build_test(newtonPhysicsPlugin
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonPhysicsComputations.cpp
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
)

pxr_build_test(testNewtonPhysicsPluginLoads
    LIBRARIES
        exec
        execUsd
        plug
        sdf
        tf
        usd
        usdPhysics
        vdf
    CPPFILES
        testenv/testPluginLoads.cpp
)

pxr_register_test(testNewtonPhysicsPluginLoads
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonPhysicsPluginLoads"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testNewtonWorldCreation
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        testenv/testWorldCreation.cpp
)

pxr_register_test(testNewtonWorldCreation
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonWorldCreation"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

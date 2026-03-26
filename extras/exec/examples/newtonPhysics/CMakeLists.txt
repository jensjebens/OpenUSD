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
        newtonPhysicsSystem.cpp
        newtonSimulationDriver.cpp
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

pxr_build_test(testNewtonBodyMapping
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        testenv/testBodyMapping.cpp
)

pxr_register_test(testNewtonBodyMapping
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonBodyMapping"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testNewtonShapeMapping
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        testenv/testShapeMapping.cpp
)

pxr_register_test(testNewtonShapeMapping
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonShapeMapping"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testNewtonMassProperties
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        testenv/testMassProperties.cpp
)

pxr_register_test(testNewtonMassProperties
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonMassProperties"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

# --- Phase 3: Integration tests ---

pxr_build_test(testNewtonFallingBox
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        newtonSimulationDriver.cpp
        testenv/testFallingBox.cpp
)

pxr_register_test(testNewtonFallingBox
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonFallingBox"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testNewtonGroundCollision
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        newtonSimulationDriver.cpp
        testenv/testGroundCollision.cpp
)

pxr_register_test(testNewtonGroundCollision
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonGroundCollision"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testNewtonMultiBody
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        newtonSimulationDriver.cpp
        testenv/testMultiBody.cpp
)

pxr_register_test(testNewtonMultiBody
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonMultiBody"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

# --- Phase 3.5: HdExec integration test ---

pxr_build_test(testNewtonExecHydraIntegration
    LIBRARIES
        ${NEWTON_PHYSICS_LIBS}
        hdExec
        hd
        execGeom
    CPPFILES
        newtonWorldManager.cpp
        usdToNewtonMapper.cpp
        newtonSimulationDriver.cpp
        newtonPhysicsSystem.cpp
        newtonPhysicsComputations.cpp
        testenv/testExecTransformWithPhysics.cpp
)

pxr_register_test(testNewtonExecHydraIntegration
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testNewtonExecHydraIntegration"
    TESTENV testenv/testNewtonPhysicsPlugin
    EXPECTED_RETURN_CODE 0
)

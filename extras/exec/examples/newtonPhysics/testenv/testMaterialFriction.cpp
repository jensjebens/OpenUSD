//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testMaterialFriction.cpp
/// \brief Tests that PhysicsMaterialAPI properties (friction and restitution)
///        are correctly read from USD material bindings and stored in the
///        mapper's body records.

#include "pxr/pxr.h"

#include "../newtonWorldManager.h"
#include "../usdToNewtonMapper.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include <cmath>
#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

static bool
_NearEqual(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) < epsilon;
}

// ---------------------------------------------------------------------------
// Test 1: Material friction from materialFriction.usda
// ---------------------------------------------------------------------------
static bool
_TestMaterialFrictionFromFile()
{
    printf("  Test: material friction from file ...\n");

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));

    UsdToNewtonMapper mapper;

    // Open the materialFriction.usda scene.
    std::string usdaPath = "materialFriction.usda";
    UsdStageRefPtr stage = UsdStage::Open(usdaPath);
    if (!stage) {
        // Try relative to testenv directory.
        stage = UsdStage::Open("testenv/materialFriction.usda");
    }
    TF_AXIOM(stage);

    mapper.MapStage(stage);

    // Scene has: Ramp (static), LowFrictionBox (dynamic),
    // HighFrictionBox (dynamic), Ground (static) = 4 bodies
    TF_AXIOM(mapper.GetBodyCount() == 4);
    TF_AXIOM(mapper.GetDynamicBodyCount() == 2);
    TF_AXIOM(mapper.GetStaticBodyCount() == 2);

    // --- LowFrictionBox: staticFriction=0.1, dynamicFriction=0.05,
    //     restitution=0.2 ---
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/LowFrictionBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.1f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.05f));
        TF_AXIOM(_NearEqual(props.restitution, 0.2f));
    }

    // --- HighFrictionBox: staticFriction=0.9, dynamicFriction=0.8,
    //     restitution=0.1 ---
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/HighFrictionBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.9f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.8f));
        TF_AXIOM(_NearEqual(props.restitution, 0.1f));
    }

    // --- Ground: no material binding => defaults ---
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(SdfPath("/World/Ground"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    // --- Ramp: no material binding => defaults ---
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(SdfPath("/World/Ramp"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    mgr.Reset();
    printf("    PASSED\n");
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: Default material properties for bodies without material bindings
// ---------------------------------------------------------------------------
static bool
_TestDefaultMaterialProperties()
{
    printf("  Test: default material properties ...\n");

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));

    UsdToNewtonMapper mapper;

    // Open fallingBox.usda — no material bindings at all.
    std::string usdaPath = "fallingBox.usda";
    UsdStageRefPtr stage = UsdStage::Open(usdaPath);
    if (!stage) {
        stage = UsdStage::Open("testenv/fallingBox.usda");
    }
    TF_AXIOM(stage);

    mapper.MapStage(stage);

    // FallingBox should have default material properties.
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/FallingBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    // Ground should also have default material properties.
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(SdfPath("/World/Ground"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    mgr.Reset();
    printf("    PASSED\n");
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: GetMaterialProperties for unmapped path returns defaults
// ---------------------------------------------------------------------------
static bool
_TestUnmappedPathReturnsDefaults()
{
    printf("  Test: unmapped path returns defaults ...\n");

    UsdToNewtonMapper mapper;
    PhysicsMaterialProperties props =
        mapper.GetMaterialProperties(SdfPath("/NonExistent/Path"));

    TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
    TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
    TF_AXIOM(_NearEqual(props.restitution, 0.0f));

    printf("    PASSED\n");
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: Inline USDA with material binding
// ---------------------------------------------------------------------------

static const char *kInlineMaterialUsda = R"usda(#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1.0
    upAxis = "Y"
    kilogramsPerUnit = 1.0
)

def Xform "World"
{
    def PhysicsScene "PhysicsScene"
    {
        vector3f physics:gravityDirection = (0, -1, 0)
        float physics:gravityMagnitude = 9.81
    }

    def Cube "BouncyBall" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        rel material:binding:physics = </World/Looks/BouncyMaterial>
        double size = 0.5
        double3 xformOp:translate = (0, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Cube "StickyBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        rel material:binding:physics = </World/Looks/StickyMaterial>
        double size = 0.5
        double3 xformOp:translate = (2, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Cube "PlainBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        double size = 0.5
        double3 xformOp:translate = (4, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Scope "Looks"
    {
        def Material "BouncyMaterial" (
            prepend apiSchemas = ["PhysicsMaterialAPI"]
        )
        {
            float physics:staticFriction = 0.3
            float physics:dynamicFriction = 0.2
            float physics:restitution = 0.95
        }

        def Material "StickyMaterial" (
            prepend apiSchemas = ["PhysicsMaterialAPI"]
        )
        {
            float physics:staticFriction = 1.0
            float physics:dynamicFriction = 0.9
            float physics:restitution = 0.0
        }
    }
}
)usda";

static bool
_TestInlineMaterialBinding()
{
    printf("  Test: inline USDA material binding ...\n");

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));

    UsdToNewtonMapper mapper;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer);
    TF_AXIOM(layer->ImportFromString(kInlineMaterialUsda));

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    mapper.MapStage(stage);

    TF_AXIOM(mapper.GetBodyCount() == 3);

    // BouncyBall — high restitution
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/BouncyBall"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.3f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.2f));
        TF_AXIOM(_NearEqual(props.restitution, 0.95f));
    }

    // StickyBox — max friction, zero restitution
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/StickyBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 1.0f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.9f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    // PlainBox — no binding, defaults
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/PlainBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    mgr.Reset();
    printf("    PASSED\n");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    printf("testMaterialFriction\n");

    bool allPassed = true;
    allPassed &= _TestMaterialFrictionFromFile();
    allPassed &= _TestDefaultMaterialProperties();
    allPassed &= _TestUnmappedPathReturnsDefaults();
    allPassed &= _TestInlineMaterialBinding();

    if (allPassed) {
        printf("testMaterialFriction PASSED\n");
        return 0;
    }
    else {
        printf("testMaterialFriction FAILED\n");
        return 1;
    }
}

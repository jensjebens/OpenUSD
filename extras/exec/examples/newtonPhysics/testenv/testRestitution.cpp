//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testRestitution.cpp
/// \brief Tests restitution (bounciness) material property reading.
///        Verifies that high and zero restitution values are correctly
///        parsed from inline USDA with PhysicsMaterialAPI bindings.

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
// Inline USDA: two boxes with different restitution over a ground plane
// ---------------------------------------------------------------------------

static const char *kRestitutionUsda = R"usda(#usda 1.0
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

    # High-restitution box — should bounce.
    def Cube "BouncyBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        rel material:binding:physics = </World/Looks/BouncyMaterial>
        double size = 0.5
        double3 xformOp:translate = (-1, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    # Zero-restitution box — should not bounce.
    def Cube "DeadBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        rel material:binding:physics = </World/Looks/DeadMaterial>
        double size = 0.5
        double3 xformOp:translate = (1, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    # Static ground
    def Cube "Ground" (
        prepend apiSchemas = ["PhysicsCollisionAPI"]
    )
    {
        double size = 1.0
        float3 xformOp:scale = (50, 0.1, 50)
        double3 xformOp:translate = (0, -0.05, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]
    }

    def Scope "Looks"
    {
        def Material "BouncyMaterial" (
            prepend apiSchemas = ["PhysicsMaterialAPI"]
        )
        {
            float physics:staticFriction = 0.5
            float physics:dynamicFriction = 0.5
            float physics:restitution = 0.9
        }

        def Material "DeadMaterial" (
            prepend apiSchemas = ["PhysicsMaterialAPI"]
        )
        {
            float physics:staticFriction = 0.5
            float physics:dynamicFriction = 0.5
            float physics:restitution = 0.0
        }
    }
}
)usda";

// ---------------------------------------------------------------------------
// Test 1: Restitution values are read correctly
// ---------------------------------------------------------------------------
static bool
_TestRestitutionReading()
{
    printf("  Test: restitution property reading ...\n");

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));

    UsdToNewtonMapper mapper;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer);
    TF_AXIOM(layer->ImportFromString(kRestitutionUsda));

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    mapper.MapStage(stage);

    // 2 dynamic boxes + 1 static ground = 3 bodies
    TF_AXIOM(mapper.GetBodyCount() == 3);
    TF_AXIOM(mapper.GetDynamicBodyCount() == 2);
    TF_AXIOM(mapper.GetStaticBodyCount() == 1);

    // BouncyBox — high restitution
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/BouncyBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.9f));
    }

    // DeadBox — zero restitution
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/DeadBox"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.5f));
        TF_AXIOM(_NearEqual(props.restitution, 0.0f));
    }

    // Ground — no material binding, defaults
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
// Test 2: Extreme restitution values
// ---------------------------------------------------------------------------

static const char *kExtremeRestitutionUsda = R"usda(#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1.0
    upAxis = "Y"
)

def Xform "World"
{
    def PhysicsScene "PhysicsScene"
    {
        vector3f physics:gravityDirection = (0, -1, 0)
        float physics:gravityMagnitude = 9.81
    }

    def Cube "MaxBounce" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        rel material:binding:physics = </World/Looks/MaxBounceMat>
        double size = 0.5
        double3 xformOp:translate = (0, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Scope "Looks"
    {
        def Material "MaxBounceMat" (
            prepend apiSchemas = ["PhysicsMaterialAPI"]
        )
        {
            float physics:staticFriction = 0.0
            float physics:dynamicFriction = 0.0
            float physics:restitution = 1.0
        }
    }
}
)usda";

static bool
_TestExtremeRestitution()
{
    printf("  Test: extreme restitution values ...\n");

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));

    UsdToNewtonMapper mapper;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer);
    TF_AXIOM(layer->ImportFromString(kExtremeRestitutionUsda));

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    mapper.MapStage(stage);

    TF_AXIOM(mapper.GetBodyCount() == 1);

    // Perfect bounce, zero friction
    {
        PhysicsMaterialProperties props =
            mapper.GetMaterialProperties(
                SdfPath("/World/MaxBounce"));
        TF_AXIOM(_NearEqual(props.staticFriction, 0.0f));
        TF_AXIOM(_NearEqual(props.dynamicFriction, 0.0f));
        TF_AXIOM(_NearEqual(props.restitution, 1.0f));
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
    printf("testRestitution\n");

    bool allPassed = true;
    allPassed &= _TestRestitutionReading();
    allPassed &= _TestExtremeRestitution();

    if (allPassed) {
        printf("testRestitution PASSED\n");
        return 0;
    }
    else {
        printf("testRestitution FAILED\n");
        return 1;
    }
}

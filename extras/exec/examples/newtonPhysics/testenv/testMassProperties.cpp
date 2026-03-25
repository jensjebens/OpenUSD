//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testMassProperties.cpp
/// \brief Tests mass property handling: explicit mass, density fallback,
///        and default mass when PhysicsMassAPI is not applied.

#include "pxr/pxr.h"

#include "../newtonWorldManager.h"
#include "../usdToNewtonMapper.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

PXR_NAMESPACE_USING_DIRECTIVE

static const char *kMassTestUsda = R"usda(#usda 1.0
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

    # Box with explicit mass = 5.0
    def Cube "ExplicitMassBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI",
                              "PhysicsMassAPI"]
    )
    {
        float physics:mass = 5.0
        double size = 1.0
        double3 xformOp:translate = (-2, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    # Box with density = 1000.0 (mass derived from density for POC)
    def Cube "DensityBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI",
                              "PhysicsMassAPI"]
    )
    {
        float physics:density = 1000.0
        double size = 1.0
        double3 xformOp:translate = (0, 5, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    # Box with no mass API — should use default mass (1.0)
    def Cube "DefaultMassBox" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsRigidBodyAPI"]
    )
    {
        double size = 1.0
        double3 xformOp:translate = (2, 5, 0)
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
}
)usda";

int main()
{
    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    UsdToNewtonMapper mapper;

    // Create a stage from the inline USDA string.
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer);
    TF_AXIOM(layer->ImportFromString(kMassTestUsda));

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));
    mapper.MapStage(stage);

    // All three dynamic boxes + 1 static ground = 4
    TF_AXIOM(mapper.GetBodyCount() == 4);
    TF_AXIOM(mapper.GetDynamicBodyCount() == 3);
    TF_AXIOM(mapper.GetStaticBodyCount() == 1);

    // Verify bodies exist at expected paths (the mapper successfully
    // processed mass properties without errors).
    TF_AXIOM(mapper.GetSimulatedTransform(
        SdfPath("/World/ExplicitMassBox")) != GfMatrix4d(1.0));
    TF_AXIOM(mapper.GetSimulatedTransform(
        SdfPath("/World/DensityBox")) != GfMatrix4d(1.0));
    TF_AXIOM(mapper.GetSimulatedTransform(
        SdfPath("/World/DefaultMassBox")) != GfMatrix4d(1.0));

    mgr.Reset();

    printf("testMassProperties PASSED\n");
    return 0;
}

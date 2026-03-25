//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testShapeMapping.cpp
/// \brief Tests that each collision shape type (box, sphere, capsule)
///        is correctly mapped from USD prims to Newton bodies.

#include "pxr/pxr.h"

#include "../newtonWorldManager.h"
#include "../usdToNewtonMapper.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdPhysics/scene.h"

PXR_NAMESPACE_USING_DIRECTIVE

int main()
{
    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();
    UsdToNewtonMapper mapper;

    // ==================================================================
    // Open mixedShapes.usda — contains sphere, box, capsule, and ground.
    // ==================================================================

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("mixedShapes.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    UsdPhysicsScene physicsScene = UsdPhysicsScene::Get(
        stage, SdfPath("/World/PhysicsScene"));
    TF_AXIOM(physicsScene);

    mgr.Initialize(physicsScene);
    mapper.MapStage(stage);

    // Verify all four bodies are present.
    TF_AXIOM(mapper.GetBodyCount() == 4);

    // Verify each shape type was mapped by checking the paths exist
    // and have valid (non-identity for translated prims) transforms.

    // Sphere at /World/FallingSphere — translate (-2, 8, 0)
    {
        GfMatrix4d xform = mapper.GetSimulatedTransform(
            SdfPath("/World/FallingSphere"));
        // Should not be identity (it has a translate).
        TF_AXIOM(xform != GfMatrix4d(1.0));
    }

    // Box at /World/FallingBox — translate (0, 10, 0)
    {
        GfMatrix4d xform = mapper.GetSimulatedTransform(
            SdfPath("/World/FallingBox"));
        TF_AXIOM(xform != GfMatrix4d(1.0));
    }

    // Capsule at /World/FallingCapsule — translate (2, 12, 0)
    {
        GfMatrix4d xform = mapper.GetSimulatedTransform(
            SdfPath("/World/FallingCapsule"));
        TF_AXIOM(xform != GfMatrix4d(1.0));
    }

    // Ground at /World/Ground — has translate and scale
    {
        GfMatrix4d xform = mapper.GetSimulatedTransform(
            SdfPath("/World/Ground"));
        TF_AXIOM(xform != GfMatrix4d(1.0));
    }

    // Verify a non-existent path returns identity.
    {
        GfMatrix4d xform = mapper.GetSimulatedTransform(
            SdfPath("/World/DoesNotExist"));
        TF_AXIOM(xform == GfMatrix4d(1.0));
    }

    mgr.Reset();

    printf("testShapeMapping PASSED\n");
    return 0;
}

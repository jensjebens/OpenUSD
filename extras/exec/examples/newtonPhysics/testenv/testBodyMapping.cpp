//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testBodyMapping.cpp
/// \brief Tests USD → Newton body mapping: body counts, kinematic
///        detection, transform readback, and re-mapping.

#include "pxr/pxr.h"

#include "../newtonWorldManager.h"
#include "../usdToNewtonMapper.h"

#include "pxr/base/gf/math.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
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
    // Test 1-6: mixedShapes.usda — 3 dynamic + 1 static ground
    // ==================================================================
    {
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

        // 3 dynamic (sphere, box, capsule) + 1 static (ground) = 4
        TF_AXIOM(mapper.GetBodyCount() == 4);
        TF_AXIOM(mapper.GetDynamicBodyCount() == 3);
        TF_AXIOM(mapper.GetStaticBodyCount() == 1);

        mgr.Reset();
    }

    // ==================================================================
    // Test 7-11: fallingBox.usda — re-map to verify Clear() works
    // ==================================================================
    {
        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        UsdPhysicsScene physicsScene = UsdPhysicsScene::Get(
            stage, SdfPath("/World/PhysicsScene"));
        TF_AXIOM(physicsScene);

        mgr.Initialize(physicsScene);
        mapper.MapStage(stage);

        // 1 dynamic (FallingBox) + 1 static (Ground) = 2
        TF_AXIOM(mapper.GetBodyCount() == 2);
        TF_AXIOM(mapper.GetDynamicBodyCount() == 1);
        TF_AXIOM(mapper.GetStaticBodyCount() == 1);

        // Verify initial transform of FallingBox is at y=10.
        GfMatrix4d xform = mapper.GetSimulatedTransform(
            SdfPath("/World/FallingBox"));
        // The translate should be (0, 10, 0).
        double y = xform[3][1];
        TF_AXIOM(GfIsClose(y, 10.0, 1e-4));

        mgr.Reset();
    }

    // ==================================================================
    // Test 12-16: kinematicAndDynamic.usda — kinematic detection
    // ==================================================================
    {
        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("kinematicAndDynamic.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        UsdPhysicsScene physicsScene = UsdPhysicsScene::Get(
            stage, SdfPath("/World/PhysicsScene"));
        TF_AXIOM(physicsScene);

        mgr.Initialize(physicsScene);
        mapper.MapStage(stage);

        // 1 kinematic + 1 dynamic + 1 static ground = 3
        // Both kinematic and dynamic count in _dynamicCount (they have
        // RigidBodyAPI).
        TF_AXIOM(mapper.GetBodyCount() == 3);
        TF_AXIOM(mapper.GetDynamicBodyCount() == 2);
        TF_AXIOM(mapper.GetStaticBodyCount() == 1);

        TF_AXIOM(mapper.IsKinematic(
            SdfPath("/World/KinematicPlatform")) == true);
        TF_AXIOM(mapper.IsKinematic(
            SdfPath("/World/DynamicBox")) == false);

        mgr.Reset();
    }

    printf("testBodyMapping PASSED\n");
    return 0;
}

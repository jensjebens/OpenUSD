//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testWorldCreation.cpp
/// \brief Tests Newton world lifecycle: creation, gravity, stepping, reset.

#include "pxr/pxr.h"

#include "../newtonWorldManager.h"

#include "pxr/base/gf/math.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdPhysics/scene.h"

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

int main()
{
    // ------------------------------------------------------------------
    // Test 1: Initialize from UsdPhysicsScene
    // ------------------------------------------------------------------

    // Open the fallingBox test asset.
    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    // Find the PhysicsScene prim.
    UsdPhysicsScene physicsScene = UsdPhysicsScene::Get(
        stage, SdfPath("/World/PhysicsScene"));
    TF_AXIOM(physicsScene);

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();

    // Initialize from the scene.
    mgr.Initialize(physicsScene);
    TF_AXIOM(mgr.IsInitialized());

    // Gravity should be approximately (0, -9.81, 0).
    GfVec3d gravity = mgr.GetGravity();
    TF_AXIOM(GfIsClose(gravity, GfVec3d(0.0, -9.81, 0.0), 1e-4));

    // Accumulated time should be zero after init.
    TF_AXIOM(mgr.GetAccumulatedTime() == 0.0);

    // ------------------------------------------------------------------
    // Test 2: Reset
    // ------------------------------------------------------------------

    mgr.Reset();
    TF_AXIOM(!mgr.IsInitialized());

    // ------------------------------------------------------------------
    // Test 3: Initialize with explicit gravity (Z-down)
    // ------------------------------------------------------------------

    mgr.Initialize(GfVec3d(0.0, 0.0, -9.81));
    TF_AXIOM(mgr.IsInitialized());
    TF_AXIOM(GfIsClose(mgr.GetGravity(), GfVec3d(0.0, 0.0, -9.81), 1e-4));

    // ------------------------------------------------------------------
    // Test 4: Step doesn't crash — normal timestep
    // ------------------------------------------------------------------

    mgr.Step(1.0 / 60.0);
    TF_AXIOM(mgr.GetAccumulatedTime() > 0.0);

    // ------------------------------------------------------------------
    // Test 5: Step doesn't crash — large timestep
    // ------------------------------------------------------------------

    mgr.Step(1.0);

    // ------------------------------------------------------------------
    // Test 6: Timestep getter/setter
    // ------------------------------------------------------------------

    double defaultTimestep = mgr.GetTimestep();
    TF_AXIOM(GfIsClose(defaultTimestep, 1.0 / 60.0, 1e-6));

    mgr.SetTimestep(1.0 / 120.0);
    TF_AXIOM(GfIsClose(mgr.GetTimestep(), 1.0 / 120.0, 1e-6));

    // ------------------------------------------------------------------
    // Test 7: Re-initialize resets accumulated time
    // ------------------------------------------------------------------

    mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));
    TF_AXIOM(mgr.GetAccumulatedTime() == 0.0);

    // ------------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------------

    mgr.Reset();
    TF_AXIOM(!mgr.IsInitialized());

    printf("testWorldCreation PASSED\n");
    return 0;
}

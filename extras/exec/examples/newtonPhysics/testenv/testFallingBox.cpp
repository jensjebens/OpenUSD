//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testFallingBox.cpp
/// \brief Integration test: Does a box actually fall under gravity?
///
/// Opens fallingBox.usda, initializes NewtonPhysicsSystem directly,
/// steps for 60 frames at 1/60s, and verifies that:
///   - The FallingBox Y position has decreased from its initial 10.0
///   - The FallingBox hasn't fallen through the ground
///   - The Ground transform is unchanged (static body)
///
/// In stub mode (no Newton), the system initializes and steps without
/// crashing, but transforms stay at their initial values.

#include "pxr/pxr.h"

#include "../newtonPhysicsSystem.h"
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
#include "pxr/usd/usdGeom/xformable.h"

PXR_NAMESPACE_USING_DIRECTIVE

int main()
{
    // ==================================================================
    // Test 1: Basic falling box simulation via NewtonPhysicsSystem
    // ==================================================================
    {
        printf("Test 1: Basic falling box simulation...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        // Initialize the physics system directly.
        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);
        TF_AXIOM(sys.IsInitialized());
        TF_AXIOM(sys.GetMapper().GetBodyCount() == 2);
        TF_AXIOM(sys.GetMapper().GetDynamicBodyCount() == 1);
        TF_AXIOM(sys.GetMapper().GetStaticBodyCount() == 1);

        // Step for 60 frames at 1/60 second each (1 second total).
        for (int i = 0; i < 60; ++i) {
            sys.AdvanceToTime((i + 1) / 60.0);
        }

        // Read transform directly from the physics system.
        GfMatrix4d xform = sys.GetSimulatedTransform(
            SdfPath("/World/FallingBox"));
        GfVec3d pos = xform.ExtractTranslation();

#ifdef NEWTON_DYNAMICS_FOUND
        // With Newton: box should have fallen significantly from y=10.
        printf("  FallingBox final Y: %.4f\n", pos[1]);
        TF_AXIOM(pos[1] < 10.0);

        // Should not have fallen through the ground.
        TF_AXIOM(pos[1] >= -1.0);
#else
        // Stub mode: transforms stay at initial values.
        printf("  [stub mode] FallingBox Y: %.4f (expected ~10.0)\n",
               pos[1]);
        TF_AXIOM(GfIsClose(pos[1], 10.0, 0.1));
#endif

        // Verify ground transform is unchanged.
        GfMatrix4d groundXform = sys.GetSimulatedTransform(
            SdfPath("/World/Ground"));
        GfVec3d groundPos = groundXform.ExtractTranslation();
        TF_AXIOM(GfIsClose(groundPos[0], 0.0, 1e-4));
        TF_AXIOM(GfIsClose(groundPos[1], -0.05, 1e-4));
        TF_AXIOM(GfIsClose(groundPos[2], 0.0, 1e-4));

        sys.Reset();

        printf("  PASSED\n");
    }

    // ==================================================================
    // Test 2: Simulation time tracking
    // ==================================================================
    {
        printf("Test 2: Simulation time tracking...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);
        TF_AXIOM(sys.IsInitialized());

        // Advance step by step and verify transforms change (or stay
        // in stub mode).
        sys.AdvanceToTime(1.0 / 60.0);
        sys.AdvanceToTime(2.0 / 60.0);

        // Just verify it doesn't crash and the system is still valid.
        GfMatrix4d xform = sys.GetSimulatedTransform(
            SdfPath("/World/FallingBox"));
        GfVec3d pos = xform.ExtractTranslation();

        // Y should be <= 10.0 (equal in stub mode, less in Newton mode).
        TF_AXIOM(pos[1] <= 10.0 + 1e-4);

        sys.Reset();

        printf("  PASSED\n");
    }

    // ==================================================================
    // Test 3: Reset clears state
    // ==================================================================
    {
        printf("Test 3: Reset clears state...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);
        TF_AXIOM(sys.IsInitialized());

        sys.AdvanceToTime(1.0 / 60.0);

        sys.Reset();
        TF_AXIOM(!sys.IsInitialized());

        printf("  PASSED\n");
    }

    printf("\ntestFallingBox PASSED\n");
    return 0;
}

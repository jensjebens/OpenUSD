//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testGroundCollision.cpp
/// \brief Focused test on ground collision: a falling box should come to
///        rest near the ground without penetrating it.
///
/// Steps the simulation for an extended period (240 frames = 4 seconds
/// at 60 fps) and verifies the box settles near ground level.

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

PXR_NAMESPACE_USING_DIRECTIVE

int main()
{
    // ==================================================================
    // Test 1: Extended simulation — box settles near ground
    // ==================================================================
    {
        printf("Test 1: Extended ground collision test...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);
        TF_AXIOM(sys.IsInitialized());

        // Step for 240 frames at 1/60s = 4 seconds of simulation.
        for (int i = 0; i < 240; ++i) {
            sys.AdvanceToTime((i + 1) / 60.0);
        }

        GfMatrix4d xform = sys.GetSimulatedTransform(
            SdfPath("/World/FallingBox"));
        GfVec3d finalPos = xform.ExtractTranslation();

        printf("  FallingBox final position: (%.4f, %.4f, %.4f)\n",
               finalPos[0], finalPos[1], finalPos[2]);

#ifdef NEWTON_DYNAMICS_FOUND
        // After 4 seconds the box should have settled near the ground.
        TF_AXIOM(finalPos[1] < 5.0);    // definitely fallen
        TF_AXIOM(finalPos[1] >= -1.0);   // not through the ground

        // X and Z should be close to the original (no lateral forces).
        TF_AXIOM(GfIsClose(finalPos[0], 0.0, 2.0));
        TF_AXIOM(GfIsClose(finalPos[2], 0.0, 2.0));
#else
        // Stub mode: position unchanged.
        printf("  [stub mode] Expected Y ≈ 10.0\n");
        TF_AXIOM(GfIsClose(finalPos[1], 10.0, 0.1));
#endif

        sys.Reset();
        printf("  PASSED\n");
    }

    // ==================================================================
    // Test 2: Box never goes below ground during simulation
    // ==================================================================
    {
        printf("Test 2: Box stays above ground during simulation...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);

        double minY = 10.0;

        for (int i = 0; i < 240; ++i) {
            sys.AdvanceToTime((i + 1) / 60.0);

            GfMatrix4d xform = sys.GetSimulatedTransform(
                SdfPath("/World/FallingBox"));
            GfVec3d pos = xform.ExtractTranslation();

            if (pos[1] < minY) {
                minY = pos[1];
            }
        }

        printf("  Minimum Y during simulation: %.4f\n", minY);

#ifdef NEWTON_DYNAMICS_FOUND
        // The box center should never go significantly below ground.
        TF_AXIOM(minY >= -2.0);  // generous tolerance
#else
        // Stub mode: Y stays at 10.0 (no simulation).
        TF_AXIOM(GfIsClose(minY, 10.0, 0.1));
#endif

        sys.Reset();
        printf("  PASSED\n");
    }

    printf("\ntestGroundCollision PASSED\n");
    return 0;
}

//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testMultiBody.cpp
/// \brief Multi-body simulation test: verifies that multiple dynamic bodies
///        with different shapes all fall under gravity, stay above ground,
///        and that static bodies don't move.

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

namespace {

/// Helper: Get translation from a simulated transform.
GfVec3d
_GetSimPos(NewtonPhysicsSystem &sys, const SdfPath &path)
{
    return sys.GetSimulatedTransform(path).ExtractTranslation();
}

} // anonymous namespace

int main()
{
    // ==================================================================
    // Test 1: Mixed shapes all fall under gravity
    // ==================================================================
    {
        printf("Test 1: Mixed shapes fall under gravity...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("mixedShapes.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);
        TF_AXIOM(sys.IsInitialized());
        TF_AXIOM(sys.GetMapper().GetBodyCount() == 4);
        TF_AXIOM(sys.GetMapper().GetDynamicBodyCount() == 3);
        TF_AXIOM(sys.GetMapper().GetStaticBodyCount() == 1);

        // Record initial positions.
        GfVec3d sphereInitial = _GetSimPos(
            sys, SdfPath("/World/FallingSphere"));
        GfVec3d boxInitial = _GetSimPos(
            sys, SdfPath("/World/FallingBox"));
        GfVec3d capsuleInitial = _GetSimPos(
            sys, SdfPath("/World/FallingCapsule"));
        GfVec3d groundInitial = _GetSimPos(
            sys, SdfPath("/World/Ground"));

        printf("  Initial: sphere=(%.1f,%.1f,%.1f) box=(%.1f,%.1f,%.1f) "
               "capsule=(%.1f,%.1f,%.1f)\n",
               sphereInitial[0], sphereInitial[1], sphereInitial[2],
               boxInitial[0], boxInitial[1], boxInitial[2],
               capsuleInitial[0], capsuleInitial[1], capsuleInitial[2]);

        // Step for 120 frames at 1/60s = 2 seconds.
        for (int i = 0; i < 120; ++i) {
            sys.AdvanceToTime((i + 1) / 60.0);
        }

        GfVec3d sphereFinal = _GetSimPos(
            sys, SdfPath("/World/FallingSphere"));
        GfVec3d boxFinal = _GetSimPos(
            sys, SdfPath("/World/FallingBox"));
        GfVec3d capsuleFinal = _GetSimPos(
            sys, SdfPath("/World/FallingCapsule"));
        GfVec3d groundFinal = _GetSimPos(
            sys, SdfPath("/World/Ground"));

        printf("  Final:   sphere=(%.2f,%.2f,%.2f) box=(%.2f,%.2f,%.2f) "
               "capsule=(%.2f,%.2f,%.2f)\n",
               sphereFinal[0], sphereFinal[1], sphereFinal[2],
               boxFinal[0], boxFinal[1], boxFinal[2],
               capsuleFinal[0], capsuleFinal[1], capsuleFinal[2]);

#ifdef NEWTON_DYNAMICS_FOUND
        // All dynamic bodies should have fallen (Y decreased).
        TF_AXIOM(sphereFinal[1] < sphereInitial[1]);
        TF_AXIOM(boxFinal[1] < boxInitial[1]);
        TF_AXIOM(capsuleFinal[1] < capsuleInitial[1]);

        // None should have fallen through the ground.
        TF_AXIOM(sphereFinal[1] >= -2.0);
        TF_AXIOM(boxFinal[1] >= -2.0);
        TF_AXIOM(capsuleFinal[1] >= -2.0);
#else
        // Stub mode: transforms stay at initial values.
        printf("  [stub mode] Positions unchanged\n");
        TF_AXIOM(GfIsClose(sphereFinal[1], sphereInitial[1], 0.1));
        TF_AXIOM(GfIsClose(boxFinal[1], boxInitial[1], 0.1));
        TF_AXIOM(GfIsClose(capsuleFinal[1], capsuleInitial[1], 0.1));
#endif

        // Ground should not have moved.
        TF_AXIOM(GfIsClose(groundFinal[0], groundInitial[0], 1e-4));
        TF_AXIOM(GfIsClose(groundFinal[1], groundInitial[1], 1e-4));
        TF_AXIOM(GfIsClose(groundFinal[2], groundInitial[2], 1e-4));

        sys.Reset();
        printf("  PASSED\n");
    }

    // ==================================================================
    // Test 2: Stacked boxes simulation
    // ==================================================================
    {
        printf("Test 2: Stacked boxes simulation...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("stackedBoxes.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
        sys.EnsureInitialized(stage);
        TF_AXIOM(sys.IsInitialized());
        TF_AXIOM(sys.GetMapper().GetBodyCount() == 4);  // 3 boxes + ground
        TF_AXIOM(sys.GetMapper().GetDynamicBodyCount() == 3);
        TF_AXIOM(sys.GetMapper().GetStaticBodyCount() == 1);

        // Record initial Y positions.
        double box1InitY = _GetSimPos(
            sys, SdfPath("/World/Box1"))[1];
        double box2InitY = _GetSimPos(
            sys, SdfPath("/World/Box2"))[1];
        double box3InitY = _GetSimPos(
            sys, SdfPath("/World/Box3"))[1];

        printf("  Initial Y: box1=%.1f box2=%.1f box3=%.1f\n",
               box1InitY, box2InitY, box3InitY);

        // Step for 120 frames at 1/60s = 2 seconds.
        for (int i = 0; i < 120; ++i) {
            sys.AdvanceToTime((i + 1) / 60.0);
        }

        double box1FinalY = _GetSimPos(
            sys, SdfPath("/World/Box1"))[1];
        double box2FinalY = _GetSimPos(
            sys, SdfPath("/World/Box2"))[1];
        double box3FinalY = _GetSimPos(
            sys, SdfPath("/World/Box3"))[1];

        printf("  Final Y: box1=%.2f box2=%.2f box3=%.2f\n",
               box1FinalY, box2FinalY, box3FinalY);

#ifdef NEWTON_DYNAMICS_FOUND
        // All boxes should still be above ground.
        TF_AXIOM(box1FinalY >= -2.0);
        TF_AXIOM(box2FinalY >= -2.0);
        TF_AXIOM(box3FinalY >= -2.0);
#else
        // Stub mode: positions unchanged.
        printf("  [stub mode] Positions unchanged\n");
        TF_AXIOM(GfIsClose(box1FinalY, box1InitY, 0.1));
        TF_AXIOM(GfIsClose(box2FinalY, box2InitY, 0.1));
        TF_AXIOM(GfIsClose(box3FinalY, box3InitY, 0.1));
#endif

        // Ground should not have moved.
        GfVec3d groundFinal = _GetSimPos(
            sys, SdfPath("/World/Ground"));
        TF_AXIOM(GfIsClose(groundFinal[1], -0.05, 1e-4));

        sys.Reset();
        printf("  PASSED\n");
    }

    printf("\ntestMultiBody PASSED\n");
    return 0;
}

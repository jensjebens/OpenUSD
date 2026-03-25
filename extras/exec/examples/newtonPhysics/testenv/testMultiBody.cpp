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

#include "../newtonSimulationDriver.h"
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

namespace {

GfVec3d
_GetTranslate(const UsdStageRefPtr &stage, const SdfPath &path)
{
    UsdPrim prim = stage->GetPrimAtPath(path);
    TF_AXIOM(prim);

    UsdGeomXformable xformable(prim);
    TF_AXIOM(xformable);

    bool resetsXformStack = false;
    std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(
        &resetsXformStack);

    for (const UsdGeomXformOp &op : ops) {
        if (op.GetOpType() == UsdGeomXformOp::TypeTranslate) {
            GfVec3d translate;
            op.Get(&translate);
            return translate;
        }
    }

    return GfVec3d(0.0);
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

        // Record initial positions.
        GfVec3d sphereInitial = _GetTranslate(
            stage, SdfPath("/World/FallingSphere"));
        GfVec3d boxInitial = _GetTranslate(
            stage, SdfPath("/World/FallingBox"));
        GfVec3d capsuleInitial = _GetTranslate(
            stage, SdfPath("/World/FallingCapsule"));
        GfVec3d groundInitial = _GetTranslate(
            stage, SdfPath("/World/Ground"));

        printf("  Initial: sphere=(%.1f,%.1f,%.1f) box=(%.1f,%.1f,%.1f) "
               "capsule=(%.1f,%.1f,%.1f)\n",
               sphereInitial[0], sphereInitial[1], sphereInitial[2],
               boxInitial[0], boxInitial[1], boxInitial[2],
               capsuleInitial[0], capsuleInitial[1], capsuleInitial[2]);

        NewtonSimulationDriver driver;
        driver.Initialize(stage);
        TF_AXIOM(driver.IsInitialized());
        TF_AXIOM(driver.GetMapper().GetBodyCount() == 4);
        TF_AXIOM(driver.GetMapper().GetDynamicBodyCount() == 3);
        TF_AXIOM(driver.GetMapper().GetStaticBodyCount() == 1);

        // Step for 120 frames at 1/60s = 2 seconds.
        const double dt = 1.0 / 60.0;
        for (int i = 0; i < 120; ++i) {
            driver.StepAndWriteBack(dt);
        }

        GfVec3d sphereFinal = _GetTranslate(
            stage, SdfPath("/World/FallingSphere"));
        GfVec3d boxFinal = _GetTranslate(
            stage, SdfPath("/World/FallingBox"));
        GfVec3d capsuleFinal = _GetTranslate(
            stage, SdfPath("/World/FallingCapsule"));
        GfVec3d groundFinal = _GetTranslate(
            stage, SdfPath("/World/Ground"));

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
        TF_AXIOM(GfIsClose(sphereFinal[1], sphereInitial[1], 1e-4));
        TF_AXIOM(GfIsClose(boxFinal[1], boxInitial[1], 1e-4));
        TF_AXIOM(GfIsClose(capsuleFinal[1], capsuleInitial[1], 1e-4));
#endif

        // Ground should not have moved.
        TF_AXIOM(GfIsClose(groundFinal[0], groundInitial[0], 1e-4));
        TF_AXIOM(GfIsClose(groundFinal[1], groundInitial[1], 1e-4));
        TF_AXIOM(GfIsClose(groundFinal[2], groundInitial[2], 1e-4));

        driver.Reset();
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

        // Record initial Y positions.
        double box1InitY = _GetTranslate(
            stage, SdfPath("/World/Box1"))[1];
        double box2InitY = _GetTranslate(
            stage, SdfPath("/World/Box2"))[1];
        double box3InitY = _GetTranslate(
            stage, SdfPath("/World/Box3"))[1];

        printf("  Initial Y: box1=%.1f box2=%.1f box3=%.1f\n",
               box1InitY, box2InitY, box3InitY);

        NewtonSimulationDriver driver;
        driver.Initialize(stage);
        TF_AXIOM(driver.IsInitialized());
        TF_AXIOM(driver.GetMapper().GetBodyCount() == 4);  // 3 boxes + ground
        TF_AXIOM(driver.GetMapper().GetDynamicBodyCount() == 3);
        TF_AXIOM(driver.GetMapper().GetStaticBodyCount() == 1);

        // Step for 120 frames at 1/60s = 2 seconds.
        const double dt = 1.0 / 60.0;
        for (int i = 0; i < 120; ++i) {
            driver.StepAndWriteBack(dt);
        }

        double box1FinalY = _GetTranslate(
            stage, SdfPath("/World/Box1"))[1];
        double box2FinalY = _GetTranslate(
            stage, SdfPath("/World/Box2"))[1];
        double box3FinalY = _GetTranslate(
            stage, SdfPath("/World/Box3"))[1];

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
        TF_AXIOM(GfIsClose(box1FinalY, box1InitY, 1e-4));
        TF_AXIOM(GfIsClose(box2FinalY, box2InitY, 1e-4));
        TF_AXIOM(GfIsClose(box3FinalY, box3InitY, 1e-4));
#endif

        // Ground should not have moved.
        GfVec3d groundFinal = _GetTranslate(
            stage, SdfPath("/World/Ground"));
        TF_AXIOM(GfIsClose(groundFinal[1], -0.05, 1e-4));

        driver.Reset();
        printf("  PASSED\n");
    }

    printf("\ntestMultiBody PASSED\n");
    return 0;
}

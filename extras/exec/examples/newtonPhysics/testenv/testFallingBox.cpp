//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testFallingBox.cpp
/// \brief Integration test: Does a box actually fall under gravity?
///
/// Opens fallingBox.usda, initializes the NewtonSimulationDriver,
/// steps for 60 frames at 1/60s, and verifies that:
///   - The FallingBox Y position has decreased from its initial 10.0
///   - The FallingBox hasn't fallen through the ground
///   - The Ground transform is unchanged (static body)
///
/// In stub mode (no Newton), the driver initializes and steps without
/// crashing, but transforms stay at their initial values.

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

/// Helper: Read xformOp:translate from a prim on the stage.
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
    // Test 1: Basic falling box simulation
    // ==================================================================
    {
        printf("Test 1: Basic falling box simulation...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        // Verify initial position.
        GfVec3d initialTranslate = _GetTranslate(
            stage, SdfPath("/World/FallingBox"));
        TF_AXIOM(GfIsClose(initialTranslate[1], 10.0, 1e-4));

        // Initialize the simulation driver.
        NewtonSimulationDriver driver;
        driver.Initialize(stage);
        TF_AXIOM(driver.IsInitialized());
        TF_AXIOM(driver.GetMapper().GetBodyCount() == 2);
        TF_AXIOM(driver.GetMapper().GetDynamicBodyCount() == 1);
        TF_AXIOM(driver.GetMapper().GetStaticBodyCount() == 1);

        // Step for 60 frames at 1/60 second each (1 second total).
        const double dt = 1.0 / 60.0;
        for (int i = 0; i < 60; ++i) {
            driver.StepAndWriteBack(dt);
        }

        // Read back the FallingBox position from the stage.
        // The driver should have authored xformOp:translate on the
        // session layer.
        GfVec3d finalTranslate = _GetTranslate(
            stage, SdfPath("/World/FallingBox"));

#ifdef NEWTON_DYNAMICS_FOUND
        // With Newton: box should have fallen significantly from y=10.
        // After 1 second of free fall: y ≈ 10 - 0.5*9.81*1² ≈ 5.1
        // With ground collision it may have already hit the ground.
        printf("  FallingBox final Y: %.4f\n", finalTranslate[1]);
        TF_AXIOM(finalTranslate[1] < 10.0);

        // Should not have fallen through the ground (y >= some negative
        // tolerance accounting for penetration and box half-height).
        TF_AXIOM(finalTranslate[1] >= -1.0);
#else
        // Stub mode: transforms stay at initial values — the driver
        // steps are no-ops so no session layer writes happen.
        printf("  [stub mode] FallingBox Y: %.4f (expected ~10.0)\n",
               finalTranslate[1]);
        TF_AXIOM(GfIsClose(finalTranslate[1], 10.0, 1e-4));
#endif

        // Verify ground transform is unchanged.
        GfVec3d groundTranslate = _GetTranslate(
            stage, SdfPath("/World/Ground"));
        TF_AXIOM(GfIsClose(groundTranslate[0], 0.0, 1e-4));
        TF_AXIOM(GfIsClose(groundTranslate[1], -0.05, 1e-4));
        TF_AXIOM(GfIsClose(groundTranslate[2], 0.0, 1e-4));

        driver.Reset();

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

        NewtonSimulationDriver driver;
        driver.Initialize(stage);
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 0.0, 1e-9));

        driver.StepAndWriteBack(1.0 / 60.0);
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 1.0 / 60.0, 1e-9));

        driver.StepAndWriteBack(1.0 / 60.0);
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 2.0 / 60.0, 1e-9));

        driver.Reset();

        printf("  PASSED\n");
    }

    // ==================================================================
    // Test 3: AdvanceToTimeCode
    // ==================================================================
    {
        printf("Test 3: AdvanceToTimeCode...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonSimulationDriver driver;
        driver.Initialize(stage);

        // The scene is at 24 fps (timeCodesPerSecond = 24).
        // Advance to time code 24 = 1 second.
        driver.AdvanceToTimeCode(UsdTimeCode(24.0), 24.0);
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 1.0, 1e-6));

        // Advance to time code 48 = 2 seconds.
        driver.AdvanceToTimeCode(UsdTimeCode(48.0), 24.0);
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 2.0, 1e-6));

        // Advance to same time code — should be a no-op.
        driver.AdvanceToTimeCode(UsdTimeCode(48.0), 24.0);
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 2.0, 1e-6));

        driver.Reset();

        printf("  PASSED\n");
    }

    // ==================================================================
    // Test 4: Reset clears state
    // ==================================================================
    {
        printf("Test 4: Reset clears state...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonSimulationDriver driver;
        driver.Initialize(stage);
        TF_AXIOM(driver.IsInitialized());

        driver.StepAndWriteBack(1.0 / 60.0);
        TF_AXIOM(driver.GetCurrentSimTime() > 0.0);

        driver.Reset();
        TF_AXIOM(!driver.IsInitialized());
        TF_AXIOM(GfIsClose(driver.GetCurrentSimTime(), 0.0, 1e-9));

        printf("  PASSED\n");
    }

    printf("\ntestFallingBox PASSED\n");
    return 0;
}

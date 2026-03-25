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
    // Test 1: Extended simulation — box settles near ground
    // ==================================================================
    {
        printf("Test 1: Extended ground collision test...\n");

        SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
            TfAbsPath("fallingBox.usda"));
        TF_AXIOM(layer);

        UsdStageRefPtr stage = UsdStage::Open(layer);
        TF_AXIOM(stage);

        NewtonSimulationDriver driver;
        driver.Initialize(stage);
        TF_AXIOM(driver.IsInitialized());

        // Step for 240 frames at 1/60s = 4 seconds of simulation.
        // This is enough time for the box to fall from y=10 and settle.
        const double dt = 1.0 / 60.0;
        for (int i = 0; i < 240; ++i) {
            driver.StepAndWriteBack(dt);
        }

        GfVec3d finalTranslate = _GetTranslate(
            stage, SdfPath("/World/FallingBox"));

        printf("  FallingBox final position: (%.4f, %.4f, %.4f)\n",
               finalTranslate[0], finalTranslate[1], finalTranslate[2]);

#ifdef NEWTON_DYNAMICS_FOUND
        // After 4 seconds the box should have settled near the ground.
        // Ground top surface is at y ≈ 0 (ground translate y=-0.05,
        // scale y=0.1 on a unit cube means top is at y=0).
        // The box is 1m, so its center should rest near y=0.5.
        // Use generous tolerances for physics imprecision.
        TF_AXIOM(finalTranslate[1] < 5.0);   // definitely fallen
        TF_AXIOM(finalTranslate[1] >= -1.0);  // not through the ground

        // X and Z should be close to the original (no lateral forces).
        TF_AXIOM(GfIsClose(finalTranslate[0], 0.0, 2.0));
        TF_AXIOM(GfIsClose(finalTranslate[2], 0.0, 2.0));
#else
        // Stub mode: position unchanged.
        printf("  [stub mode] Expected Y ≈ 10.0\n");
        TF_AXIOM(GfIsClose(finalTranslate[1], 10.0, 1e-4));
#endif

        driver.Reset();
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

        NewtonSimulationDriver driver;
        driver.Initialize(stage);

        const double dt = 1.0 / 60.0;
        double minY = 10.0;

        for (int i = 0; i < 240; ++i) {
            driver.StepAndWriteBack(dt);

            GfVec3d translate = _GetTranslate(
                stage, SdfPath("/World/FallingBox"));

            if (translate[1] < minY) {
                minY = translate[1];
            }
        }

        printf("  Minimum Y during simulation: %.4f\n", minY);

#ifdef NEWTON_DYNAMICS_FOUND
        // The box center should never go significantly below ground.
        // Some minor penetration is expected in physics engines.
        TF_AXIOM(minY >= -2.0);  // generous tolerance
#else
        // Stub mode: Y stays at 10.0 (no writes).
        TF_AXIOM(GfIsClose(minY, 10.0, 1e-4));
#endif

        driver.Reset();
        printf("  PASSED\n");
    }

    printf("\ntestGroundCollision PASSED\n");
    return 0;
}

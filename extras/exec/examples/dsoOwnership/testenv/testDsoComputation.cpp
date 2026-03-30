//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testDsoComputation.cpp
/// \brief Standalone test for the DSO OpenExec computation.
///
/// Verifies computeEffectiveWorldTransform at multiple frames across
/// the carrier switch (Robot → AGV at frame 100).

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/execGeom/tokens.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/xformable.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeEffectiveWorldTransform)
);

static bool
_Vec3dClose(const GfVec3d &a, const GfVec3d &b, double eps = 1e-4)
{
    return (a - b).GetLength() < eps;
}

/// Compute expected sphere world position from stage data (ground truth).
/// Same logic as bake_result.py: effective = offset * carrierWorld.
static GfVec3d
_ComputeExpectedFromStage(
    ExecUsdSystem &execSystem,
    const UsdStageRefPtr &stage,
    const UsdPrim &sphere,
    const UsdPrim &robot,
    const UsdPrim &agv,
    double frame)
{
    UsdTimeCode tc(frame);
    execSystem.ChangeTime(tc);

    // Get carrier world transforms via exec
    std::vector<ExecUsdValueKey> carrierKeys = {
        {robot, ExecGeomXformableTokens->computeLocalToWorldTransform},
        {agv,   ExecGeomXformableTokens->computeLocalToWorldTransform},
    };
    ExecUsdRequest carrierReq =
        execSystem.BuildRequest(std::move(carrierKeys));
    execSystem.PrepareRequest(carrierReq);
    ExecUsdCacheView carrierView = execSystem.Compute(carrierReq);

    GfMatrix4d robotWorld = carrierView.Get(0).Get<GfMatrix4d>();
    GfMatrix4d agvWorld   = carrierView.Get(1).Get<GfMatrix4d>();

    // Read activeCarrierIndex and localOffset
    int idx = 0;
    sphere.GetAttribute(TfToken("dynamicOwnership:activeCarrierIndex"))
          .Get(&idx, tc);
    GfMatrix4d offset(1.0);
    sphere.GetAttribute(TfToken("dynamicOwnership:localOffset"))
          .Get(&offset, tc);

    GfMatrix4d carrierWorld = (idx == 0) ? robotWorld : agvWorld;
    GfMatrix4d expected = offset * carrierWorld;
    return expected.ExtractTranslation();
}

int main(int argc, char *argv[])
{
    // 1. Register plugin resources
    const PlugPluginPtrVector plugins =
        PlugRegistry::GetInstance().RegisterPlugins(TfAbsPath("resources"));

    if (plugins.empty()) {
        std::cerr << "FAIL: Could not load plugin from resources/plugInfo.json"
                  << std::endl;
        return 1;
    }
    std::cout << "Plugin loaded: " << plugins[0]->GetName() << std::endl;

    // Also register schema resources (for DynamicOwnershipAPI type)
    // Look for them relative to the test binary or via env var.
    const char *schemaPath = std::getenv("DSO_SCHEMA_RESOURCES");
    if (schemaPath && schemaPath[0] != '\0') {
        PlugRegistry::GetInstance().RegisterPlugins(TfAbsPath(schemaPath));
    }

    // 2. Open the test stage
    std::string stagePath = (argc > 1)
        ? std::string(argv[1])
        : "test_stage.usda";

    UsdStageRefPtr stage = UsdStage::Open(stagePath);
    if (!stage) {
        std::cerr << "FAIL: Could not open stage: " << stagePath << std::endl;
        return 1;
    }
    std::cout << "Stage opened: " << stagePath << std::endl;

    // 3. Get prims
    UsdPrim sphere = stage->GetPrimAtPath(SdfPath("/Factory/Parts/Sphere_001"));
    UsdPrim robot  = stage->GetPrimAtPath(SdfPath("/Factory/Robot"));
    UsdPrim agv    = stage->GetPrimAtPath(SdfPath("/Factory/AGV"));

    if (!sphere.IsValid() || !robot.IsValid() || !agv.IsValid()) {
        std::cerr << "FAIL: Could not find sphere, robot, or AGV prim"
                  << std::endl;
        return 1;
    }

    if (!sphere.HasAPI(TfToken("DynamicOwnershipAPI"))) {
        std::cerr << "WARNING: DynamicOwnershipAPI not found in apiSchemas."
                  << std::endl;
    }

    // 4. Create ExecUsdSystem
    ExecUsdSystem execSystem(stage);
    std::cout << "ExecUsdSystem created" << std::endl;

    // 5. Test at key frames across the carrier switch
    struct TestCase {
        double frame;
        const char *description;
    };

    TestCase cases[] = {
        {  1.0, "Robot carrier, start"},
        { 50.0, "Robot carrier, mid"},
        { 99.0, "Robot carrier, pre-switch"},
        {100.0, "AGV carrier, at switch"},
        {150.0, "AGV carrier, mid"},
        {200.0, "AGV carrier, end"},
    };
    const int numCases = sizeof(cases) / sizeof(cases[0]);

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < numCases; ++i) {
        TestCase &tc = cases[i];
        std::cout << "\n--- Frame " << tc.frame
                  << " (" << tc.description << ") ---" << std::endl;

        // Compute expected from stage data
        GfVec3d expected = _ComputeExpectedFromStage(
            execSystem, stage, sphere, robot, agv, tc.frame);

        // Run the DSO computation
        execSystem.ChangeTime(UsdTimeCode(tc.frame));

        std::vector<ExecUsdValueKey> keys = {
            {sphere, _tokens->computeEffectiveWorldTransform},
        };

        ExecUsdRequest request =
            execSystem.BuildRequest(std::move(keys));

        if (!request.IsValid()) {
            std::cerr << "  FAIL: Request invalid" << std::endl;
            ++failed;
            continue;
        }

        execSystem.PrepareRequest(request);
        ExecUsdCacheView view = execSystem.Compute(request);

        VtValue v0 = view.Get(0);

        if (!v0.IsHolding<GfMatrix4d>()) {
            std::cerr << "  FAIL: Unexpected value type: "
                      << (v0.IsEmpty() ? "empty" : v0.GetTypeName())
                      << std::endl;
            ++failed;
            continue;
        }

        GfMatrix4d sphereXform = v0.Get<GfMatrix4d>();
        GfVec3d spherePos = sphereXform.ExtractTranslation();

        std::cout << "  Sphere effective: ("
                  << spherePos[0] << ", " << spherePos[1] << ", "
                  << spherePos[2] << ")" << std::endl;
        std::cout << "  Expected:         ("
                  << expected[0] << ", " << expected[1] << ", "
                  << expected[2] << ")" << std::endl;

        bool ok = _Vec3dClose(spherePos, expected);
        std::cout << "  Result: " << (ok ? "PASS" : "FAIL") << std::endl;

        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
    }

    // 6. Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "DSO Computation Test Results" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << " out of " << numCases << " test cases" << std::endl;
    std::cout << "========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}

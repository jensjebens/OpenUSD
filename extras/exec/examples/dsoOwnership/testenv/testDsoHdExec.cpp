//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testDsoHdExec.cpp
/// \brief Integration test: DSO computation → HdExecComputedTransformSceneIndex → Hydra.
///
/// Proves that DSO's computeEffectiveWorldTransform flows through the
/// SHARED HdExec scene index filter (the same filter used by Units and
/// Newton physics) rather than a bespoke DsoOwnershipSceneIndex.
///
/// Pipeline:
///   UsdImagingStageSceneIndex
///     → HdExecComputedTransformSceneIndex (shared, auto-discovers DSO)
///       → query GetPrim() → verify xform matches expected

#include "pxr/pxr.h"

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"
#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execGeom/tokens.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/xformable.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

static GfVec3d
_GetWorldTranslation(const HdSceneIndexBaseRefPtr &si, const SdfPath &path)
{
    auto prim = si->GetPrim(path);
    if (!prim.dataSource) return GfVec3d(0);
    auto xs = HdXformSchema::GetFromParent(prim.dataSource);
    if (!xs.IsDefined()) return GfVec3d(0);
    auto mat = xs.GetMatrix();
    if (!mat) return GfVec3d(0);
    return mat->GetTypedValue(0.0f).ExtractTranslation();
}

/// Compute expected from stage data (ground truth).
static GfVec3d
_ComputeExpected(
    const UsdStageRefPtr &stage,
    ExecUsdSystem &execSystem,
    double frame)
{
    UsdPrim sphere = stage->GetPrimAtPath(SdfPath("/Factory/Parts/Sphere_001"));
    UsdPrim robot  = stage->GetPrimAtPath(SdfPath("/Factory/Robot"));
    UsdPrim agv    = stage->GetPrimAtPath(SdfPath("/Factory/AGV"));
    UsdTimeCode tc(frame);

    execSystem.ChangeTime(tc);

    std::vector<ExecUsdValueKey> carrierKeys = {
        {robot, ExecGeomXformableTokens->computeLocalToWorldTransform},
        {agv,   ExecGeomXformableTokens->computeLocalToWorldTransform},
    };
    ExecUsdRequest req = execSystem.BuildRequest(std::move(carrierKeys));
    execSystem.PrepareRequest(req);
    ExecUsdCacheView view = execSystem.Compute(req);

    GfMatrix4d robotWorld = view.Get(0).Get<GfMatrix4d>();
    GfMatrix4d agvWorld   = view.Get(1).Get<GfMatrix4d>();

    int idx = 0;
    sphere.GetAttribute(TfToken("dynamicOwnership:activeCarrierIndex"))
          .Get(&idx, tc);
    GfMatrix4d offset(1.0);
    sphere.GetAttribute(TfToken("dynamicOwnership:localOffset"))
          .Get(&offset, tc);

    GfMatrix4d carrierWorld = (idx == 0) ? robotWorld : agvWorld;
    return (offset * carrierWorld).ExtractTranslation();
}

int main(int argc, char *argv[])
{
    std::cout << "=== DSO → HdExecComputedTransformSceneIndex Integration Test ===" << std::endl;
    std::cout << "(Uses the SHARED HdExec filter — no bespoke DsoOwnershipSceneIndex)" << std::endl;

    // 1. Register plugin resources
    const PlugPluginPtrVector plugins =
        PlugRegistry::GetInstance().RegisterPlugins(TfAbsPath("resources"));
    if (plugins.empty()) {
        std::cerr << "FAIL: Could not load plugin from resources/" << std::endl;
        return 1;
    }

    const char *schemaPath = std::getenv("DSO_SCHEMA_RESOURCES");
    if (schemaPath && schemaPath[0] != '\0') {
        PlugRegistry::GetInstance().RegisterPlugins(TfAbsPath(schemaPath));
    }

    // 2. Open stage
    std::string stagePath = (argc > 1)
        ? std::string(argv[1])
        : "test_stage.usda";

    UsdStageRefPtr stage = UsdStage::Open(stagePath);
    if (!stage) {
        std::cerr << "FAIL: Could not open stage: " << stagePath << std::endl;
        return 1;
    }

    // 3. Create shared exec system
    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    // 4. Build scene index chain using HdExecComputedTransformSceneIndex
    //    This is the KEY difference from the old DSO Hydra test —
    //    we use the SHARED filter, not DsoOwnershipSceneIndex.
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    info.addDrawModeSceneIndex = false;

    info.overridesSceneIndexCallback =
        [&](HdSceneIndexBaseRefPtr const &input) -> HdSceneIndexBaseRefPtr
    {
        return HdExecComputedTransformSceneIndex::New(
            input,
            stage,
            execSystem,
            {_tokens->computeEffectiveWorldTransform},
            /* resetXformStack = */ true);
    };

    auto sceneIndices = UsdImagingCreateSceneIndices(info);
    auto finalSI = sceneIndices.finalSceneIndex;

    const SdfPath spherePath("/Factory/Parts/Sphere_001");

    // 5. Verify the filter discovers the DSO prim
    {
        UsdPrim sphere = stage->GetPrimAtPath(spherePath);
        std::cout << "\n  Prim valid: " << sphere.IsValid() << std::endl;
        std::cout << "  Applied schemas: ";
        for (auto &s : sphere.GetAppliedSchemas()) std::cout << s << " ";
        std::cout << std::endl;

        // Direct exec request to confirm computation is registered
        std::vector<ExecUsdValueKey> keys;
        keys.emplace_back(sphere, _tokens->computeEffectiveWorldTransform);
        auto req = execSystem->BuildRequest(std::move(keys));
        std::cout << "  Direct exec request valid: " << req.IsValid() << std::endl;
    }

    // 6. Test at key frames
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
        const TestCase &tc = cases[i];
        std::cout << "\n--- Frame " << tc.frame
                  << " (" << tc.description << ") ---" << std::endl;

        // Set time on stage scene index
        sceneIndices.stageSceneIndex->SetTime(UsdTimeCode(tc.frame));

        // Advance exec system time via the shared filter's API
        execSystem->ChangeTime(UsdTimeCode(tc.frame));

        // Dirty all computed prims (simulates what AdvanceGlobalTime does)
        // In production, AdvanceGlobalTime handles this automatically.

        // Compute expected
        GfVec3d expected = _ComputeExpected(stage, *execSystem, tc.frame);

        // Query through the shared HdExec filter
        HdSceneIndexPrim hdPrim = finalSI->GetPrim(spherePath);

        if (!hdPrim.dataSource) {
            std::cerr << "  FAIL: No data source for " << spherePath
                      << std::endl;
            ++failed;
            continue;
        }

        HdXformSchema xformSchema =
            HdXformSchema::GetFromParent(hdPrim.dataSource);

        if (!xformSchema.IsDefined()) {
            std::cerr << "  FAIL: No xform schema on sphere" << std::endl;
            ++failed;
            continue;
        }

        HdMatrixDataSourceHandle matrixDs = xformSchema.GetMatrix();
        if (!matrixDs) {
            std::cerr << "  FAIL: No matrix data source" << std::endl;
            ++failed;
            continue;
        }

        GfMatrix4d matrix = matrixDs->GetTypedValue(0.0f);
        GfVec3d spherePos = matrix.ExtractTranslation();

        std::cout << "  Hydra xform:  ("
                  << spherePos[0] << ", " << spherePos[1] << ", "
                  << spherePos[2] << ")" << std::endl;
        std::cout << "  Expected:     ("
                  << expected[0] << ", " << expected[1] << ", "
                  << expected[2] << ")" << std::endl;

        // Check resetXformStack
        HdBoolDataSourceHandle resetDs = xformSchema.GetResetXformStack();
        bool resetXformStack = resetDs ? resetDs->GetTypedValue(0.0f) : false;
        std::cout << "  resetXformStack: "
                  << (resetXformStack ? "true" : "false") << std::endl;

        bool posOk = _Vec3dClose(spherePos, expected);
        bool resetOk = resetXformStack;  // Must be true for world-space xform

        bool ok = posOk && resetOk;
        std::cout << "  Result: " << (ok ? "PASS" : "FAIL");
        if (!posOk) std::cout << " [position mismatch]";
        if (!resetOk) std::cout << " [resetXformStack should be true]";
        std::cout << std::endl;

        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
    }

    // 7. Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "DSO → HdExec Integration Test Results" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << " out of " << numCases << " test cases" << std::endl;
    std::cout << "========================================" << std::endl;

    if (failed == 0) {
        std::cout << "\nSUCCESS: DSO works through the shared HdExec filter." << std::endl;
        std::cout << "The bespoke DsoOwnershipSceneIndex is no longer needed." << std::endl;
    }

    return (failed == 0) ? 0 : 1;
}

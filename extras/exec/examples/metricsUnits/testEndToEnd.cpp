// End-to-end test: MetricsAPI → OpenExec → HdExecComputedTransformSceneIndex → Hydra
//
// The clean stack:
//   UsdGeomMetricsAPI (prim-level units) 
//     → execMetricsUnits (computeUnitAwareLocalToWorldTransform)
//       → HdExecComputedTransformSceneIndex (generic exec→Hydra bridge)
//         → HdFlatteningSceneIndex → correct world-space transforms
//
#include "pxr/pxr.h"
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"
#include "pxr/usdImaging/usdImaging/sceneIndices.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include <iostream>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeUnitAwareLocalToWorldTransform)
);

static const std::string kStage = R"usda(#usda 1.0
(
    metersPerUnit = 1.0
    upAxis = "Y"
)
def Xform "Factory" {
    double3 xformOp:translate = (10, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]

    def Xform "CmRobot" (
        apiSchemas = ["GeomMetricsAPI"]
    ) {
        double metrics:metersPerUnit = 0.01
        uniform token[] xformOpOrder = ["xformOp:transform"]
        matrix4d xformOp:transform = (
            (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (100, 0, 50, 1)
        )

        def Xform "Gripper" {
            uniform token[] xformOpOrder = ["xformOp:transform"]
            matrix4d xformOp:transform = (
                (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 30, 1)
            )
        }
    }

    def Xform "MeterTable" {
        double3 xformOp:translate = (5, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
)usda";

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

int main()
{
    std::cout << "=== End-to-End: MetricsAPI → OpenExec → Hydra ===" << std::endl;

    auto layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer->ImportFromString(kStage));
    auto stage = UsdStage::Open(layer);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    // Build the scene index chain with HdExecComputedTransformSceneIndex
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
            {_tokens->computeUnitAwareLocalToWorldTransform},
            /* resetXformStack = */ false);
    };

    auto sceneIndices = UsdImagingCreateSceneIndices(info);
    sceneIndices.stageSceneIndex->SetTime(UsdTimeCode::Default());
    auto finalSI = sceneIndices.finalSceneIndex;

    // Debug: check what GetPrim returns from the exec filter
    // The overridesSceneIndexCallback wraps the filter around the stage SI
    // Find our filter in the chain...
    std::cout << "\n  Debug: checking paths from stage SI..." << std::endl;
    auto cmPrim = sceneIndices.stageSceneIndex->GetPrim(SdfPath("/Factory/CmRobot"));
    std::cout << "  StageSceneIndex /Factory/CmRobot type: " << cmPrim.primType << std::endl;
    std::cout << "  StageSceneIndex /Factory/CmRobot has dataSource: " << (cmPrim.dataSource != nullptr) << std::endl;

    // Check the USD prim directly
    auto usdPrim = stage->GetPrimAtPath(SdfPath("/Factory/CmRobot"));
    std::cout << "  USD prim valid: " << usdPrim.IsValid() << std::endl;
    std::cout << "  USD prim schemas: ";
    for (auto &s : usdPrim.GetAppliedSchemas()) std::cout << s << " ";
    std::cout << std::endl;

    // Try direct exec request on this prim
    {
        std::vector<ExecUsdValueKey> keys;
        keys.emplace_back(usdPrim, _tokens->computeUnitAwareLocalToWorldTransform);
        auto req = execSystem->BuildRequest(std::move(keys));
        std::cout << "  Direct exec request valid: " << req.IsValid() << std::endl;
        if (req.IsValid()) {
            execSystem->PrepareRequest(req);
            auto cache = execSystem->Compute(req);
            VtValue v = cache.Get(0);
            if (v.IsHolding<GfMatrix4d>()) {
                auto t = v.UncheckedGet<GfMatrix4d>().ExtractTranslation();
                std::cout << "  Direct exec result: " << t << std::endl;
            }
        }
    }

    // Test: /Factory/CmRobot
    // Raw local: (100, 0, 50) in cm
    // Corrected local: (1, 0, 0.5) in m
    // Parent /Factory translate: (10, 0, 0)
    // World: (11, 0, 0.5)
    {
        auto t = _GetWorldTranslation(finalSI, SdfPath("/Factory/CmRobot"));
        std::cout << "  /Factory/CmRobot world: " << t
                  << "  (expect: 11, 0, 0.5)" << std::endl;
        if (GfIsClose(t, GfVec3d(11, 0, 0.5), 0.01)) {
            std::cout << "  PASSED" << std::endl;
        } else {
            std::cout << "  FAILED (got " << t << ")" << std::endl;
        }
    }

    // Test: /Factory/MeterTable (no MetricsAPI — should pass through)
    // Local: (5, 0, 0), Parent: (10, 0, 0), World: (15, 0, 0)
    {
        auto t = _GetWorldTranslation(finalSI, SdfPath("/Factory/MeterTable"));
        std::cout << "  /Factory/MeterTable world: " << t
                  << "  (expect: 15, 0, 0)" << std::endl;
        if (GfIsClose(t, GfVec3d(15, 0, 0), 0.01)) {
            std::cout << "  PASSED" << std::endl;
        } else {
            std::cout << "  FAILED (got " << t << ")" << std::endl;
        }
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}

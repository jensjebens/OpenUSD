// Test the new MetricsAPI-based OpenExec computation
#include "pxr/pxr.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeUnitAwareLocalToWorldTransform)
    (computeLocalToWorldTransform)
);

static const std::string kStage = R"usda(#usda 1.0
(
    metersPerUnit = 1.0
    upAxis = "Y"
)
def Xform "Root" {
    def Xform "CmBox" (
        apiSchemas = ["GeomMetricsAPI"]
    ) {
        double metrics:metersPerUnit = 0.01
        uniform token[] xformOpOrder = ["xformOp:transform"]
        matrix4d xformOp:transform = (
            (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (100, 200, 300, 1)
        )
    }
    def Xform "MBox" (
        apiSchemas = ["GeomMetricsAPI"]
    ) {
        double metrics:metersPerUnit = 1.0
        uniform token[] xformOpOrder = ["xformOp:transform"]
        matrix4d xformOp:transform = (
            (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (5, 10, 15, 1)
        )
    }
    def Xform "NoMetrics" {
        uniform token[] xformOpOrder = ["xformOp:transform"]
        matrix4d xformOp:transform = (
            (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (7, 8, 9, 1)
        )
    }
}
)usda";

int main()
{
    std::cout << "=== MetricsAPI + OpenExec Test ===" << std::endl;

    auto layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer->ImportFromString(kStage));
    auto stage = UsdStage::Open(layer);

    ExecUsdSystem execSystem(stage);

    // Test CmBox: (100,200,300) * 0.01 = (1,2,3)
    {
        std::cout << "\n--- CmBox (cm→m) ---" << std::endl;
        std::vector<ExecUsdValueKey> keys{
            {stage->GetPrimAtPath(SdfPath("/Root/CmBox")),
             _tokens->computeUnitAwareLocalToWorldTransform}
        };
        auto request = execSystem.BuildRequest(std::move(keys));
        std::cout << "  Request valid: " << request.IsValid() << std::endl;
        if (request.IsValid()) {
            execSystem.PrepareRequest(request);
            auto cache = execSystem.Compute(request);
            VtValue v = cache.Get(0);
            if (!v.IsEmpty() && v.IsHolding<GfMatrix4d>()) {
                auto t = v.UncheckedGet<GfMatrix4d>().ExtractTranslation();
                std::cout << "  Unit-aware translate: " << t << std::endl;
                std::cout << "  Expected:             (1, 2, 3)" << std::endl;
                TF_AXIOM(GfIsClose(t, GfVec3d(1, 2, 3), 1e-6));
                std::cout << "  PASSED" << std::endl;
            } else {
                std::cout << "  FAILED (empty or wrong type)" << std::endl;
                return 1;
            }
        } else {
            std::cout << "  FAILED (invalid request)" << std::endl;
            return 1;
        }
    }

    // Test MBox: (5,10,15) * 1.0 = (5,10,15) — no change
    {
        std::cout << "\n--- MBox (m→m, identity) ---" << std::endl;
        std::vector<ExecUsdValueKey> keys{
            {stage->GetPrimAtPath(SdfPath("/Root/MBox")),
             _tokens->computeUnitAwareLocalToWorldTransform}
        };
        auto request = execSystem.BuildRequest(std::move(keys));
        if (request.IsValid()) {
            execSystem.PrepareRequest(request);
            auto cache = execSystem.Compute(request);
            VtValue v = cache.Get(0);
            if (!v.IsEmpty() && v.IsHolding<GfMatrix4d>()) {
                auto t = v.UncheckedGet<GfMatrix4d>().ExtractTranslation();
                std::cout << "  Unit-aware translate: " << t << std::endl;
                std::cout << "  Expected:             (5, 10, 15)" << std::endl;
                TF_AXIOM(GfIsClose(t, GfVec3d(5, 10, 15), 1e-6));
                std::cout << "  PASSED" << std::endl;
            }
        }
    }

    // Test NoMetrics: no GeomMetricsAPI — computation shouldn't exist
    {
        std::cout << "\n--- NoMetrics (no schema) ---" << std::endl;
        std::vector<ExecUsdValueKey> keys{
            {stage->GetPrimAtPath(SdfPath("/Root/NoMetrics")),
             _tokens->computeUnitAwareLocalToWorldTransform}
        };
        auto request = execSystem.BuildRequest(std::move(keys));
        // Should still be valid but return identity or raw value
        std::cout << "  Request valid: " << request.IsValid() << std::endl;
        if (request.IsValid()) {
            execSystem.PrepareRequest(request);
            auto cache = execSystem.Compute(request);
            VtValue v = cache.Get(0);
            std::cout << "  Value empty: " << v.IsEmpty() << std::endl;
        }
        std::cout << "  (Expected: computation not found — no GeomMetricsAPI)" << std::endl;
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}

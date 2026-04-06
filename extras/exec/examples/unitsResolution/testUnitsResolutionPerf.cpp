//
// testExecUnitsPerf.cpp — Performance overhead measurement for unit-aware
// transform computation vs standard computeLocalToWorldTransform.
//
// Creates a hierarchy of N prims, half with UnitsResolutionAPI applied
// (cm→m correction), half without. Measures evaluation time for both
// standard and unit-aware computations.
//
#include "pxr/pxr.h"

#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/stopwatch.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeUnitAwareLocalToWorldTransform)
    (computeLocalToWorldTransform)
    ((unitScale, "unitsResolution:metersPerUnitScale"))
);


/// Build a flat hierarchy of N Xform prims under /Root.
/// If withUnitsApi is true, applies UnitsResolutionAPI and authors the scale.
static UsdStageRefPtr
_BuildTestStage(int numPrims, bool withUnitsApi, double unitScale = 0.01)
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);

    UsdGeomXform::Define(stage, SdfPath("/Root"));

    for (int i = 0; i < numPrims; ++i) {
        std::ostringstream path;
        path << "/Root/Prim_" << i;

        UsdGeomXform xform = UsdGeomXform::Define(stage, SdfPath(path.str()));
        
        // Author a transform matrix with a translation
        UsdGeomXformOp op = xform.AddTransformOp();
        GfMatrix4d mat(1.0);
        mat.SetRow3(3, GfVec3d(100.0 * (i + 1), 200.0, 300.0));
        op.Set(mat);

        if (withUnitsApi) {
            // Apply the API schema and author the unit scale
            UsdPrim prim = xform.GetPrim();
            prim.ApplyAPI(TfType::FindByName("UnitsResolutionAPI"));
            prim.CreateAttribute(
                _tokens->unitScale,
                SdfValueTypeNames->Double,
                /* custom = */ false).Set(unitScale);
        }
    }

    return stage;
}


struct PerfResult {
    int numPrims;
    double buildRequestMs;
    double prepareMs;
    double computeMs;
    double totalMs;
};


static PerfResult
_RunPerfTest(
    const UsdStageRefPtr &stage,
    const TfToken &computationName,
    int numPrims)
{
    using Clock = std::chrono::high_resolution_clock;

    ExecUsdSystem execSystem(stage);

    // Build value keys for all prims
    auto t0 = Clock::now();

    std::vector<ExecUsdValueKey> valueKeys;
    valueKeys.reserve(numPrims);
    for (int i = 0; i < numPrims; ++i) {
        std::ostringstream path;
        path << "/Root/Prim_" << i;
        UsdPrim prim = stage->GetPrimAtPath(SdfPath(path.str()));
        TF_AXIOM(prim);
        valueKeys.push_back({prim, computationName});
    }

    ExecUsdRequest request = execSystem.BuildRequest(std::move(valueKeys));
    auto t1 = Clock::now();
    TF_AXIOM(request.IsValid());

    execSystem.PrepareRequest(request);
    auto t2 = Clock::now();
    TF_AXIOM(request.IsValid());

    ExecUsdCacheView cache = execSystem.Compute(request);
    auto t3 = Clock::now();

    // Validate first result
    VtValue v = cache.Get(0);
    TF_AXIOM(!v.IsEmpty());

    PerfResult result;
    result.numPrims = numPrims;
    result.buildRequestMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.prepareMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    result.computeMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    result.totalMs = std::chrono::duration<double, std::milli>(t3 - t0).count();

    return result;
}


static void
_PrintResult(const char *label, const PerfResult &r)
{
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  " << label << " (" << r.numPrims << " prims):"
              << "  build=" << r.buildRequestMs << "ms"
              << "  prepare=" << r.prepareMs << "ms"
              << "  compute=" << r.computeMs << "ms"
              << "  total=" << r.totalMs << "ms"
              << "  (" << std::setprecision(1)
              << (r.totalMs / r.numPrims * 1000.0) << " µs/prim)"
              << std::endl;
}


int main(int argc, char **argv)
{
    // Register our plugin
    const PlugPluginPtrVector plugins = PlugRegistry::GetInstance()
        .RegisterPlugins(TfAbsPath("resources"));

    int counts[] = {100, 1000, 5000, 10000};
    int numCounts = sizeof(counts) / sizeof(counts[0]);

    // Warm-up run
    {
        auto warmupStage = _BuildTestStage(10, false);
        ExecUsdSystem sys(warmupStage);
        std::vector<ExecUsdValueKey> keys {
            {warmupStage->GetPrimAtPath(SdfPath("/Root/Prim_0")),
             _tokens->computeLocalToWorldTransform}
        };
        auto req = sys.BuildRequest(std::move(keys));
        sys.PrepareRequest(req);
        sys.Compute(req);
    }

    std::cout << "=== OpenExec Unit-Aware Transform Performance ===" << std::endl;
    std::cout << std::endl;

    for (int c = 0; c < numCounts; ++c) {
        int n = counts[c];
        std::cout << "--- " << n << " prims ---" << std::endl;

        // Standard L2W (no unit correction)
        {
            auto stage = _BuildTestStage(n, false);
            auto result = _RunPerfTest(stage, _tokens->computeLocalToWorldTransform, n);
            _PrintResult("Standard L2W    ", result);
        }

        // Unit-aware L2W (with UnitsResolutionAPI)
        {
            auto stage = _BuildTestStage(n, true, 0.01);
            auto result = _RunPerfTest(stage, _tokens->computeUnitAwareLocalToWorldTransform, n);
            _PrintResult("Unit-aware L2W  ", result);
        }

        // Unit-aware L2W with identity scale (unitScale = 1.0, early-out path)
        {
            auto stage = _BuildTestStage(n, true, 1.0);
            auto result = _RunPerfTest(stage, _tokens->computeUnitAwareLocalToWorldTransform, n);
            _PrintResult("Unit-aware (1.0)", result);
        }

        std::cout << std::endl;
    }

    std::cout << "=== Done ===" << std::endl;
    return 0;
}

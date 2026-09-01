// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "tessellator.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usdGeom/gprim.h"

// usd-brep (SMLib). SmuConvert and SmuTessellate take pxr types, so the kernel must be built
// against the same OpenUSD this plugin links; see CMakeLists.txt.
#include "SmuConvert.h"
#include "SmuTessellate.h"
#include "SmApiBrep.h"
#include "SmApiGeneral.h"
#include "SmBrep.h"
#include "SmContext.h"
#include "SmCoreTypes.h"
#include "SmPoly.h"     // SmPolyBrep
#include "SmTypes.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// SmApiCreateContext installs a process-wide SmContext that owns all kernel
// allocations. Creating it more than once leaks the previous one, so do it
// exactly once however many tessellators exist.
SmContext *
_GetSmContext()
{
    static std::once_flag once;
    std::call_once(once, [] { SmApiCreateContext(); });
    return SmApiGetOrCreateContext();
}

} // anonymous namespace

HdUsdBrepTessellator::HdUsdBrepTessellator() = default;
HdUsdBrepTessellator::~HdUsdBrepTessellator() = default;

std::vector<HdUsdBrepTessellationResult>
HdUsdBrepTessellator::Tessellate(
    const UsdPrim &brepArrayPrim,
    const HdUsdBrepTessellationParams &params) const
{
    std::vector<HdUsdBrepTessellationResult> results;

    const UsdGeomGprim gprim(brepArrayPrim);
    if (!gprim) {
        HdUsdBrepTessellationResult r;
        r.errorMessage = "prim is not a UsdGeomGprim: "
                       + brepArrayPrim.GetPath().GetString();
        results.push_back(std::move(r));
        return results;
    }

    SmContext *context = _GetSmContext();
    if (!context) {
        HdUsdBrepTessellationResult r;
        r.errorMessage = "could not create a usd-brep kernel context";
        results.push_back(std::move(r));
        return results;
    }

    // Schema arrays to kernel Breps. This is the whole reason hdUsdBrep links
    // usd-brep's USD layer rather than reimplementing the reader: BrepArray is
    // usd-brep's own serialisation, so it reconstructs topology directly.
    std::vector<SmBrep *> breps;
    const SmStatus readStatus = SMU_BrepConvert::BrepMove_UsdToSMLib(
        *context, gprim, breps, params.healerEnabled ? TRUE : FALSE);

    if (readStatus != SM_SUCCESS || breps.empty()) {
        HdUsdBrepTessellationResult r;
        r.errorMessage = "BrepMove_UsdToSMLib failed on "
                       + brepArrayPrim.GetPath().GetString();
        results.push_back(std::move(r));
        return results;
    }

    results.reserve(breps.size());

    for (size_t i = 0; i < breps.size(); ++i) {
        HdUsdBrepTessellationResult r;
        r.brepIndex = static_cast<int>(i);

        SmBrep *brep = breps[i];
        if (!brep) {
            r.errorMessage = "null Brep at index " + std::to_string(i);
            results.push_back(std::move(r));
            continue;
        }

        // Unconditional, and it needs to stay that way. An analytic surface
        // rebuilt from the schema is not guaranteed to meet SmApiTessellate's
        // preconditions, and SmApiTurnToNurbs is a no-op on a surface that is
        // already NURBS -- so do not make this conditional on the face's
        // surface type.
        SmApiTurnToNurbs(brep);

        SmPolyBrep *polyBrep = nullptr;
        const SmApiStatus tessStatus = SmApiTessellate(
            brep,
            params.chordHeightTolerance,
            params.angleToleranceDegrees,
            params.maxEdgeLength,
            params.maxAspectRatio,
            polyBrep);

        if (tessStatus != SM_SUCCESS || !polyBrep) {
            r.errorMessage = "SmApiTessellate failed on Brep "
                           + std::to_string(i);
            results.push_back(std::move(r));
            continue;
        }

        const SmStatus meshStatus = SMU_BrepConvert::GetMeshData(
            *polyBrep,
            r.extent,
            r.points,
            r.faceVertexCounts,
            r.faceVertexIndices,
            r.normals,
            r.normalsIndices,
            r.normalsInterpolation,
            r.subdivisionScheme,
            &r.geomSubsetIndices,
            nullptr);

        if (meshStatus != SM_SUCCESS) {
            r.errorMessage = "GetMeshData failed on Brep " + std::to_string(i);
            results.push_back(std::move(r));
            continue;
        }

        r.success = true;
        results.push_back(std::move(r));
    }

    return results;
}

PXR_NAMESPACE_CLOSE_SCOPE

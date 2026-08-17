// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "tessellator.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usdGeom/gprim.h"

// SMLib. SmuConvert and SmuTessellate take pxr types, so SMLib must be built
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

UsdSolidSmlibTessellator::UsdSolidSmlibTessellator() = default;
UsdSolidSmlibTessellator::~UsdSolidSmlibTessellator() = default;

std::vector<UsdSolidSmlibTessellationResult>
UsdSolidSmlibTessellator::Tessellate(
    const UsdPrim &brepArrayPrim,
    const UsdSolidSmlibTessellationParams &params) const
{
    std::vector<UsdSolidSmlibTessellationResult> results;

    const UsdGeomGprim gprim(brepArrayPrim);
    if (!gprim) {
        UsdSolidSmlibTessellationResult r;
        r.errorMessage = "prim is not a UsdGeomGprim: "
                       + brepArrayPrim.GetPath().GetString();
        results.push_back(std::move(r));
        return results;
    }

    SmContext *context = _GetSmContext();
    if (!context) {
        UsdSolidSmlibTessellationResult r;
        r.errorMessage = "could not create an SMLib context";
        results.push_back(std::move(r));
        return results;
    }

    // Schema arrays to kernel Breps. This is the whole reason hdSmlib links
    // SMLib's USD layer rather than reimplementing the reader: BrepArray is
    // SMLib's own serialisation, so it reconstructs topology directly.
    std::vector<SmBrep *> breps;
    const SmStatus readStatus = SMU_BrepConvert::BrepMove_UsdToSMLib(
        *context, gprim, breps, params.healerEnabled ? TRUE : FALSE);

    if (readStatus != SM_SUCCESS || breps.empty()) {
        UsdSolidSmlibTessellationResult r;
        r.errorMessage = "BrepMove_UsdToSMLib failed on "
                       + brepArrayPrim.GetPath().GetString();
        results.push_back(std::move(r));
        return results;
    }

    results.reserve(breps.size());

    for (size_t i = 0; i < breps.size(); ++i) {
        UsdSolidSmlibTessellationResult r;
        r.brepIndex = static_cast<int>(i);

        SmBrep *brep = breps[i];
        if (!brep) {
            r.errorMessage = "null Brep at index " + std::to_string(i);
            results.push_back(std::move(r));
            continue;
        }

        // Analytic surfaces that have been through a USD round trip can leave
        // the kernel in a state where SmApiTessellate crashes. SmApiTurnToNurbs
        // converts them in place and is a no-op on surfaces that are already
        // NURBS, so it is unconditional here rather than conditional on the
        // face's surface type.
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

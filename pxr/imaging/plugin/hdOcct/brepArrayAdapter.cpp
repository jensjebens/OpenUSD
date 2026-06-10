// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// UsdImaging adapter implementation for BrepArray prims.
// Maps BrepArray -> Hydra "generativeProcedural" type.
// Pre-tessellates using UsdSolidTessellator and injects mesh data
// as a custom data source that the procedural reads.

#include "brepArrayAdapter.h"
#include "tessellator.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/type.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdGp/generativeProcedural.h"
#include "pxr/usdImaging/usdImaging/dataSourcePrim.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/tokens.h"
#include "pxr/usd/usdGeom/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (usdSolidTessellation)
    (usdSolidTessellatorData)
    (meshCount)
    (points)
    (faceVertexCounts)
    (faceVertexIndices)
    (normals)
);

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdSolidBrepArrayAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

// Helper: Build a container data source holding tessellation results.
// The procedural reads this to emit child mesh prims without needing
// stage access.
static HdContainerDataSourceHandle
_BuildTessellationDataSource(const UsdPrim &prim)
{
    UsdSolidTessellator tessellator;
    UsdSolidTessellationParams params;
    // Use defaults (0.1 linear, 0.5 angular) — overrides can come from primvars

    // Check for tessellation parameter overrides
    {
        UsdAttribute attr;
        attr = prim.GetAttribute(TfToken("primvars:tessellation:linearDeflection"));
        if (attr) {
            double val;
            if (attr.Get(&val)) params.linearDeflection = val;
        }
        attr = prim.GetAttribute(TfToken("primvars:tessellation:angularDeflection"));
        if (attr) {
            double val;
            if (attr.Get(&val)) params.angularDeflection = val;
        }
        attr = prim.GetAttribute(TfToken("primvars:tessellation:computeNormals"));
        if (attr) {
            bool val;
            if (attr.Get(&val)) params.computeNormals = val;
        }
    }

    std::vector<UsdSolidTessellationResult> results =
        tessellator.Tessellate(prim, params);

    // Filter to successful results
    std::vector<const UsdSolidTessellationResult*> goodResults;
    for (const auto& r : results) {
        if (r.success && !r.points.empty()) {
            goodResults.push_back(&r);
        }
    }

    if (goodResults.empty()) {
        TF_WARN("BrepArrayAdapter: No tessellation results for '%s'",
                prim.GetPath().GetText());
        return HdRetainedContainerDataSource::New(
            _tokens->meshCount,
            HdRetainedTypedSampledDataSource<int>::New(0));
    }

    TF_STATUS("BrepArrayAdapter: Tessellated '%s' -> %zu mesh(es)",
              prim.GetPath().GetText(), goodResults.size());

    // Pack each mesh's data into a numbered child container:
    // usdSolidTessellatorData/meshCount -> int
    // usdSolidTessellatorData/mesh_0/points -> VtArray<GfVec3f>
    // usdSolidTessellatorData/mesh_0/faceVertexCounts -> VtArray<int>
    // etc.

    // Build per-mesh data sources
    std::vector<TfToken> meshNames;
    std::vector<HdDataSourceBaseHandle> meshDataSources;

    for (size_t i = 0; i < goodResults.size(); ++i) {
        const auto& result = *goodResults[i];

        // Convert double points to float
        VtArray<GfVec3f> floatPoints(result.points.size());
        for (size_t j = 0; j < result.points.size(); ++j) {
            const GfVec3d& p = result.points[j];
            floatPoints[j] = GfVec3f(
                (float)p[0], (float)p[1], (float)p[2]);
        }

        HdContainerDataSourceHandle meshDs;
        if (!result.normals.empty()) {
            meshDs = HdRetainedContainerDataSource::New(
                _tokens->points,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    floatPoints),
                _tokens->faceVertexCounts,
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    result.faceVertexCounts),
                _tokens->faceVertexIndices,
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    result.faceVertexIndices),
                _tokens->normals,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    result.normals));
        } else {
            meshDs = HdRetainedContainerDataSource::New(
                _tokens->points,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    floatPoints),
                _tokens->faceVertexCounts,
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    result.faceVertexCounts),
                _tokens->faceVertexIndices,
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    result.faceVertexIndices));
        }

        meshNames.push_back(
            TfToken(TfStringPrintf("mesh_%zu", i)));
        meshDataSources.push_back(meshDs);
    }

    // Add mesh count
    meshNames.push_back(_tokens->meshCount);
    meshDataSources.push_back(
        HdRetainedTypedSampledDataSource<int>::New(
            (int)goodResults.size()));

    return HdRetainedContainerDataSource::New(
        meshNames.size(),
        meshNames.data(),
        meshDataSources.data());
}

// ----------------------------------------------------------------------------
// Scene Index API (modern path — Storm uses this)
// ----------------------------------------------------------------------------

TfTokenVector
UsdSolidBrepArrayAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    TF_STATUS("BrepArrayAdapter::GetImagingSubprims called for '%s'",
              prim.GetPath().GetText());
    return { TfToken() };
}

TfToken
UsdSolidBrepArrayAdapter::GetImagingSubprimType(
    UsdPrim const& prim,
    TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        TF_STATUS("BrepArrayAdapter::GetImagingSubprimType -> generativeProcedural for '%s'",
                  prim.GetPath().GetText());
        return HdGpGenerativeProceduralTokens->generativeProcedural;
    }
    return TfToken();
}

HdContainerDataSourceHandle
UsdSolidBrepArrayAdapter::GetImagingSubprimData(
    UsdPrim const& prim,
    TfToken const& subprim,
    const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (!subprim.IsEmpty()) {
        return nullptr;
    }

    // Base prim data (xform, visibility, purpose, etc.)
    HdContainerDataSourceHandle baseDs =
        UsdImagingDataSourcePrim::New(
            prim.GetPath(), prim, stageGlobals);

    // Synthesize hdGp:proceduralType primvar
    HdContainerDataSourceHandle procTypePrimvar =
        HdRetainedContainerDataSource::New(
            HdPrimvarSchemaTokens->primvarValue,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                _tokens->usdSolidTessellation),
            HdPrimvarSchemaTokens->interpolation,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                HdPrimvarSchemaTokens->constant),
            HdPrimvarSchemaTokens->role,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                TfToken()));

    HdContainerDataSourceHandle primvarsOverlay =
        HdRetainedContainerDataSource::New(
            HdGpGenerativeProceduralTokens->proceduralType,
            procTypePrimvar);

    // Get existing primvars from base data source
    HdContainerDataSourceHandle basePrimvars;
    if (baseDs) {
        basePrimvars = HdContainerDataSource::Cast(
            baseDs->Get(HdPrimvarsSchemaTokens->primvars));
    }

    HdContainerDataSourceHandle fullPrimvars;
    if (basePrimvars) {
        fullPrimvars = HdOverlayContainerDataSource::New(
            primvarsOverlay, basePrimvars);
    } else {
        fullPrimvars = primvarsOverlay;
    }

    // Pre-tessellate and inject mesh data
    HdContainerDataSourceHandle tessData = _BuildTessellationDataSource(prim);

    // Compose: our primvars + tessellation data overlaid on base
    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdPrimvarsSchemaTokens->primvars, fullPrimvars,
            _tokens->usdSolidTessellatorData, tessData),
        baseDs);
}

HdDataSourceLocatorSet
UsdSolidBrepArrayAdapter::InvalidateImagingSubprim(
    UsdPrim const& prim,
    TfToken const& subprim,
    TfTokenVector const& properties,
    UsdImagingPropertyInvalidationType invalidationType)
{
    if (!subprim.IsEmpty()) {
        return HdDataSourceLocatorSet();
    }

    return UsdImagingDataSourcePrim::Invalidate(
        prim, subprim, properties, invalidationType);
}

// ----------------------------------------------------------------------------
// Legacy Render Index API
// ----------------------------------------------------------------------------

SdfPath
UsdSolidBrepArrayAdapter::Populate(
    UsdPrim const& prim,
    UsdImagingIndexProxy* index,
    UsdImagingInstancerContext const* instancerContext)
{
    const SdfPath cachePath = ResolveCachePath(
        prim.GetPath(), instancerContext);

    index->InsertRprim(
        HdGpGenerativeProceduralTokens->generativeProcedural,
        cachePath, prim,
        instancerContext ? instancerContext->instancerAdapter
                         : UsdImagingPrimAdapterSharedPtr());

    return cachePath;
}

bool
UsdSolidBrepArrayAdapter::IsSupported(
    UsdImagingIndexProxy const* index) const
{
    return true;
}

void
UsdSolidBrepArrayAdapter::TrackVariability(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    HdDirtyBits* timeVaryingBits,
    UsdImagingInstancerContext const* instancerContext) const
{
    _IsTransformVarying(prim,
        HdChangeTracker::DirtyTransform,
        UsdImagingTokens->usdVaryingXform,
        timeVaryingBits);

    _IsVarying(prim,
        UsdGeomTokens->visibility,
        HdChangeTracker::DirtyVisibility,
        UsdImagingTokens->usdVaryingVisibility,
        timeVaryingBits,
        true);
}

void
UsdSolidBrepArrayAdapter::UpdateForTime(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    UsdImagingInstancerContext const* instancerContext) const
{
    // Static geometry — nothing to update per-frame.
}

HdDirtyBits
UsdSolidBrepArrayAdapter::ProcessPropertyChange(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& propertyName)
{
    return HdChangeTracker::AllDirty;
}

void
UsdSolidBrepArrayAdapter::MarkDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    HdDirtyBits dirty,
    UsdImagingIndexProxy* index)
{
    index->MarkRprimDirty(cachePath, dirty);
}

void
UsdSolidBrepArrayAdapter::MarkTransformDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyTransform);
}

void
UsdSolidBrepArrayAdapter::MarkVisibilityDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyVisibility);
}

void
UsdSolidBrepArrayAdapter::_RemovePrim(
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    index->RemoveRprim(cachePath);
}

PXR_NAMESPACE_CLOSE_SCOPE

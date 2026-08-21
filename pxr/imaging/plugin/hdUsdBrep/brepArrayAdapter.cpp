// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// UsdImaging adapter implementation for BrepArray prims.
// Maps BrepArray -> Hydra "generativeProcedural" type.
// Pre-tessellates using HdUsdBrepTessellator and injects mesh data
// as a custom data source that the procedural reads.

#include "brepArrayAdapter.h"
#include "tessellator.h"

#include "pxr/base/gf/math.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/type.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"
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
    (usdBrepTessellation)
    (usdBrepTessellatorData)
    (meshCount)
    (points)
    (faceVertexCounts)
    (faceVertexIndices)
    (normals)
);

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = HdUsdBrepArrayAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

// Helper: Build a container data source holding tessellation results.
// The procedural reads this to emit child mesh prims without needing
// stage access.
static HdContainerDataSourceHandle
_BuildTessellationDataSource(const UsdPrim &prim)
{
    HdUsdBrepTessellator tessellator;
    HdUsdBrepTessellationParams params;
    // The primvar names are OpenCASCADE's, and deliberately kept: the point of
    // having both plugins is that one fixture drives both kernels identically.
    // usd-brep names the same two quantities differently and takes the angular
    // one in degrees, so translate rather than rename.
    //
    //   primvars:tessellation:linearDeflection  -> chordHeightTolerance
    //       both are the maximum distance between a facet and the true surface
    //   primvars:tessellation:angularDeflection -> angleToleranceDegrees
    //       both are the maximum angle between adjacent facet normals; OCCT
    //       takes radians, usd-brep takes degrees
    //
    // computeNormals has no counterpart: usd-brep's GetMeshData always returns
    // normals, so the primvar is read and ignored rather than silently changing
    // meaning.
    {
        UsdAttribute attr;
        attr = prim.GetAttribute(TfToken("primvars:tessellation:linearDeflection"));
        if (attr) {
            double val;
            if (attr.Get(&val)) params.chordHeightTolerance = val;
        }
        attr = prim.GetAttribute(TfToken("primvars:tessellation:angularDeflection"));
        if (attr) {
            double val;
            if (attr.Get(&val)) params.angleToleranceDegrees = GfRadiansToDegrees(val);
        }
    }

    std::vector<HdUsdBrepTessellationResult> results =
        tessellator.Tessellate(prim, params);

    // Filter to successful results
    std::vector<const HdUsdBrepTessellationResult*> goodResults;
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

    // Pack each mesh's data into a numbered child container:
    // usdBrepTessellatorData/meshCount -> int
    // usdBrepTessellatorData/mesh_0/points -> VtArray<GfVec3f>
    // usdBrepTessellatorData/mesh_0/faceVertexCounts -> VtArray<int>
    // etc.

    // Build per-mesh data sources
    std::vector<TfToken> meshNames;
    std::vector<HdDataSourceBaseHandle> meshDataSources;

    for (size_t i = 0; i < goodResults.size(); ++i) {
        const auto& result = *goodResults[i];

        // Compact vertices: remove unreferenced points and remap indices.
        // OCCT tessellation can produce vertices unused by final triangulation.
        std::vector<int> oldToNew(result.points.size(), -1);
        int newIdx = 0;
        for (int idx : result.faceVertexIndices) {
            if (idx >= 0 && (size_t)idx < result.points.size()
                && oldToNew[idx] == -1) {
                oldToNew[idx] = newIdx++;
            }
        }

        VtArray<GfVec3f> floatPoints(newIdx);
        for (size_t j = 0; j < result.points.size(); ++j) {
            if (oldToNew[j] >= 0) {
                const GfVec3d& p = result.points[j];
                floatPoints[oldToNew[j]] = GfVec3f(
                    (float)p[0], (float)p[1], (float)p[2]);
            }
        }

        VtArray<int> remappedIndices(result.faceVertexIndices.size());
        for (size_t j = 0; j < result.faceVertexIndices.size(); ++j) {
            remappedIndices[j] = oldToNew[result.faceVertexIndices[j]];
        }

        HdContainerDataSourceHandle meshDs;
        if (!result.normals.empty()) {
            VtArray<GfVec3f> compactNormals(newIdx);
            for (size_t j = 0; j < result.normals.size()
                 && j < result.points.size(); ++j) {
                if (oldToNew[j] >= 0) {
                    compactNormals[oldToNew[j]] = result.normals[j];
                }
            }
            meshDs = HdRetainedContainerDataSource::New(
                _tokens->points,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    floatPoints),
                _tokens->faceVertexCounts,
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    result.faceVertexCounts),
                _tokens->faceVertexIndices,
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    remappedIndices),
                _tokens->normals,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    compactNormals));
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
                    remappedIndices));
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
HdUsdBrepArrayAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
HdUsdBrepArrayAdapter::GetImagingSubprimType(
    UsdPrim const& prim,
    TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        return HdGpGenerativeProceduralTokens->generativeProcedural;
    }
    return TfToken();
}

HdContainerDataSourceHandle
HdUsdBrepArrayAdapter::GetImagingSubprimData(
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
                _tokens->usdBrepTessellation),
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
            _tokens->usdBrepTessellatorData, tessData),
        baseDs);
}

HdDataSourceLocatorSet
HdUsdBrepArrayAdapter::InvalidateImagingSubprim(
    UsdPrim const& prim,
    TfToken const& subprim,
    TfTokenVector const& properties,
    UsdImagingPropertyInvalidationType invalidationType)
{
    if (!subprim.IsEmpty()) {
        return HdDataSourceLocatorSet();
    }

    // Base prim invalidation handles xform / visibility / purpose so moving
    // or hiding the prim stays live.
    HdDataSourceLocatorSet result =
        UsdImagingDataSourcePrim::Invalidate(
            prim, subprim, properties, invalidationType);

    // The tessellated mesh + edge data is built eagerly in
    // GetImagingSubprimData and injected as a retained snapshot; the stage
    // scene index does NOT rebuild that snapshot on a value-only change, it
    // only dirties locators. So when any attribute that feeds the tessellation
    // changes (B-rep geometry/topology or the tessellation params), we ask the
    // index to fully repopulate this prim: GetImagingSubprimData then re-runs
    // the OCCT build + mesh with the new values and the generative procedural
    // re-cooks, refreshing both the mesh and the edge curves. (OCCT has no
    // incremental rebuild, so a full repopulate is the right granularity.)
    static const char* const kGeomPrefixes[] = {
        "brep:", "region:", "shell:", "faceuse:", "face:", "loop:",
        "edgeuse:", "edge:", "wireEdge:", "vertex:",
        "primvars:tessellation:"
    };
    for (const TfToken& p : properties) {
        const std::string& name = p.GetString();
        for (const char* pre : kGeomPrefixes) {
            if (TfStringStartsWith(name, pre)) {
                result.insert(HdDataSourceLocator(
                    UsdImagingTokens->stageSceneIndexRepopulate));
                return result;
            }
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// Legacy Render Index API
// ----------------------------------------------------------------------------

SdfPath
HdUsdBrepArrayAdapter::Populate(
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
HdUsdBrepArrayAdapter::IsSupported(
    UsdImagingIndexProxy const* index) const
{
    return true;
}

void
HdUsdBrepArrayAdapter::TrackVariability(
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
HdUsdBrepArrayAdapter::UpdateForTime(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    UsdImagingInstancerContext const* instancerContext) const
{
    // Static geometry — nothing to update per-frame.
}

HdDirtyBits
HdUsdBrepArrayAdapter::ProcessPropertyChange(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& propertyName)
{
    return HdChangeTracker::AllDirty;
}

void
HdUsdBrepArrayAdapter::MarkDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    HdDirtyBits dirty,
    UsdImagingIndexProxy* index)
{
    // In the scene index path (USD 0.26+), Populate() is never called,
    // so no rprim exists at this path. Only dirty if populated.
    if (index->IsPopulated(cachePath)) {
        index->MarkRprimDirty(cachePath, dirty);
    }
}

void
HdUsdBrepArrayAdapter::MarkTransformDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    if (index->IsPopulated(cachePath)) {
        index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyTransform);
    }
}

void
HdUsdBrepArrayAdapter::MarkVisibilityDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    if (index->IsPopulated(cachePath)) {
        index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyVisibility);
    }
}

void
HdUsdBrepArrayAdapter::_RemovePrim(
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    index->RemoveRprim(cachePath);
}

PXR_NAMESPACE_CLOSE_SCOPE

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
    (usdSolidTessellation)
    (usdSolidTessellatorData)
    (meshCount)
    (edgeCount)
    (tangentEdgeCount)
    (points)
    (faceVertexCounts)
    (faceVertexIndices)
    (curveVertexCounts)
    (normals)
    (displayColor)
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

    // Authored per-face display colors (converter face-level CAD styles):
    // primvars:displayColor on the BrepArray prim with one entry per B-rep
    // face. A single entry is the ordinary constant color and stays on the
    // procedural's inherited-constant path.
    VtArray<GfVec3f> faceColors;
    {
        UsdAttribute attr =
            prim.GetAttribute(TfToken("primvars:displayColor"));
        if (attr) {
            VtArray<GfVec3f> v;
            if (attr.Get(&v) && v.size() > 1) {
                faceColors = v;
            }
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

        std::vector<TfToken> mNames;
        std::vector<HdDataSourceBaseHandle> mVals;
        mNames.push_back(_tokens->points);
        mVals.push_back(
            HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                floatPoints));
        mNames.push_back(_tokens->faceVertexCounts);
        mVals.push_back(
            HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                result.faceVertexCounts));
        mNames.push_back(_tokens->faceVertexIndices);
        mVals.push_back(
            HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                remappedIndices));
        if (!result.normals.empty()) {
            VtArray<GfVec3f> compactNormals(newIdx);
            for (size_t j = 0; j < result.normals.size()
                 && j < result.points.size(); ++j) {
                if (oldToNew[j] >= 0) {
                    compactNormals[oldToNew[j]] = result.normals[j];
                }
            }
            mNames.push_back(_tokens->normals);
            mVals.push_back(
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    compactNormals));
        }

        // Expand authored per-face colors to per-triangle (uniform interp)
        // via faceSolidFaceIndices. Only for single-Brep prims: the authored
        // array is indexed by global face id, and faceSolidFaceIndices
        // restarts at 0 per Brep, so they only coincide when there is one.
        if (!faceColors.empty() && goodResults.size() == 1
            && result.faceSolidFaceIndices.size()
                   == result.faceVertexCounts.size()) {
            const size_t nTris = result.faceVertexCounts.size();
            VtArray<GfVec3f> triColors(nTris);
            bool ok = true;
            for (size_t t = 0; t < nTris; ++t) {
                const int f = result.faceSolidFaceIndices[t];
                if (f < 0 || (size_t)f >= faceColors.size()) {
                    ok = false;
                    break;
                }
                triColors[t] = faceColors[f];
            }
            if (ok) {
                mNames.push_back(_tokens->displayColor);
                mVals.push_back(
                    HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                        triColors));
            } else {
                TF_WARN("BrepArrayAdapter: face index out of range of the "
                        "%zu authored per-face colors on '%s'; falling back "
                        "to constant color", faceColors.size(),
                        prim.GetPath().GetText());
            }
        }

        HdContainerDataSourceHandle meshDs =
            HdRetainedContainerDataSource::New(
                mNames.size(), mNames.data(), mVals.data());

        meshNames.push_back(
            TfToken(TfStringPrintf("mesh_%zu", i)));
        meshDataSources.push_back(meshDs);

        // B-rep edge polylines (for the edge/line display): pack as an
        // edges_<i> container holding points + curveVertexCounts, consumed by
        // the procedural as a linear HdBasisCurves child.
        if (!result.edgePoints.empty()) {
            VtArray<GfVec3f> edgePts(result.edgePoints.size());
            for (size_t j = 0; j < result.edgePoints.size(); ++j) {
                const GfVec3d& p = result.edgePoints[j];
                edgePts[j] = GfVec3f((float)p[0], (float)p[1], (float)p[2]);
            }
            HdContainerDataSourceHandle edgeDs =
                HdRetainedContainerDataSource::New(
                    _tokens->points,
                    HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                        edgePts),
                    _tokens->curveVertexCounts,
                    HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                        result.edgeCurveVertexCounts));
            meshNames.push_back(
                TfToken(TfStringPrintf("edges_%zu", i)));
            meshDataSources.push_back(edgeDs);
        }

        // Tangent (smooth) edges -> a parallel tangent_edges_<i> container,
        // emitted as a separate, independently toggleable curve child.
        if (!result.tangentEdgePoints.empty()) {
            VtArray<GfVec3f> tpts(result.tangentEdgePoints.size());
            for (size_t j = 0; j < result.tangentEdgePoints.size(); ++j) {
                const GfVec3d& p = result.tangentEdgePoints[j];
                tpts[j] = GfVec3f((float)p[0], (float)p[1], (float)p[2]);
            }
            HdContainerDataSourceHandle tDs =
                HdRetainedContainerDataSource::New(
                    _tokens->points,
                    HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                        tpts),
                    _tokens->curveVertexCounts,
                    HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                        result.tangentEdgeCurveVertexCounts));
            meshNames.push_back(
                TfToken(TfStringPrintf("tangent_edges_%zu", i)));
            meshDataSources.push_back(tDs);
        }
    }

    // Add mesh count
    meshNames.push_back(_tokens->meshCount);
    meshDataSources.push_back(
        HdRetainedTypedSampledDataSource<int>::New(
            (int)goodResults.size()));

    // Edge-group count (parallel to mesh count; edges_<i> may be absent when
    // a Brep produced no drawable edges).
    meshNames.push_back(_tokens->edgeCount);
    meshDataSources.push_back(
        HdRetainedTypedSampledDataSource<int>::New(
            (int)goodResults.size()));

    // Tangent-edge-group count (parallel; tangent_edges_<i> may be absent).
    meshNames.push_back(_tokens->tangentEdgeCount);
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
    return { TfToken() };
}

TfToken
UsdSolidBrepArrayAdapter::GetImagingSubprimType(
    UsdPrim const& prim,
    TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
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
    // In the scene index path (USD 0.26+), Populate() is never called,
    // so no rprim exists at this path. Only dirty if populated.
    if (index->IsPopulated(cachePath)) {
        index->MarkRprimDirty(cachePath, dirty);
    }
}

void
UsdSolidBrepArrayAdapter::MarkTransformDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    if (index->IsPopulated(cachePath)) {
        index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyTransform);
    }
}

void
UsdSolidBrepArrayAdapter::MarkVisibilityDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    if (index->IsPopulated(cachePath)) {
        index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyVisibility);
    }
}

void
UsdSolidBrepArrayAdapter::_RemovePrim(
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    index->RemoveRprim(cachePath);
}

PXR_NAMESPACE_CLOSE_SCOPE

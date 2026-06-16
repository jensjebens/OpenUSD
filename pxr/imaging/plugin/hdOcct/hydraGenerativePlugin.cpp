// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Hydra Generative Procedural Plugin implementation.
// Reads pre-tessellated mesh data injected by BrepArrayAdapter and
// emits child mesh prims into the Hydra scene index.

#include "hydraGenerativePlugin.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/type.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/basisCurvesSchema.h"
#include "pxr/imaging/hd/basisCurvesTopologySchema.h"
#include "pxr/imaging/hd/purposeSchema.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hdGp/generativeProceduralPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
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

// --------------------------------------------------------------------------
// UsdSolidTessellationProcedural
// --------------------------------------------------------------------------

UsdSolidTessellationProcedural::UsdSolidTessellationProcedural(
    const SdfPath &proceduralPrimPath)
    : HdGpGenerativeProcedural(proceduralPrimPath)
{
}

UsdSolidTessellationProcedural::~UsdSolidTessellationProcedural() = default;

HdGpGenerativeProcedural::DependencyMap
UsdSolidTessellationProcedural::UpdateDependencies(
    const HdSceneIndexBaseRefPtr &inputScene)
{
    DependencyMap deps;
    // Depend on our own prim so we get dirtied when the adapter re-tessellates
    deps[_GetProceduralPrimPath()] = HdDataSourceLocatorSet(
        HdDataSourceLocator(_tokens->usdSolidTessellatorData));
    return deps;
}

void
UsdSolidTessellationProcedural::_Tessellate(
    const HdSceneIndexBaseRefPtr &inputScene)
{
    _meshes.clear();
    _edges.clear();
    _tangentEdges.clear();

    const SdfPath primPath = _GetProceduralPrimPath();
    HdSceneIndexPrim prim = inputScene->GetPrim(primPath);

    if (!prim.dataSource) {
        TF_WARN("UsdSolidTessellationProcedural: No data source for '%s'",
                primPath.GetText());
        return;
    }

    // Read pre-tessellated data from the adapter-injected container
    HdContainerDataSourceHandle tessDs =
        HdContainerDataSource::Cast(
            prim.dataSource->Get(_tokens->usdSolidTessellatorData));

    if (!tessDs) {
        TF_WARN("UsdSolidTessellationProcedural: No tessellation data for '%s'. "
                "BrepArrayAdapter may not have injected it.",
                primPath.GetText());
        return;
    }

    // Read mesh count
    int meshCount = 0;
    if (auto countDs = HdTypedSampledDataSource<int>::Cast(
            tessDs->Get(_tokens->meshCount))) {
        meshCount = countDs->GetTypedValue(0.0f);
    }

    if (meshCount <= 0) {
        TF_WARN("UsdSolidTessellationProcedural: meshCount=0 for '%s'",
                primPath.GetText());
        return;
    }

    // Read each mesh
    for (int i = 0; i < meshCount; ++i) {
        TfToken meshName(TfStringPrintf("mesh_%d", i));
        HdContainerDataSourceHandle meshDs =
            HdContainerDataSource::Cast(tessDs->Get(meshName));

        if (!meshDs) {
            TF_WARN("UsdSolidTessellationProcedural: Missing mesh data "
                    "'%s' for '%s'", meshName.GetText(), primPath.GetText());
            continue;
        }

        _MeshData mesh;

        // Points
        if (auto pointsDs =
                HdTypedSampledDataSource<VtArray<GfVec3f>>::Cast(
                    meshDs->Get(_tokens->points))) {
            mesh.points = pointsDs->GetTypedValue(0.0f);
        }

        // Face vertex counts
        if (auto fvcDs =
                HdTypedSampledDataSource<VtArray<int>>::Cast(
                    meshDs->Get(_tokens->faceVertexCounts))) {
            mesh.faceVertexCounts = fvcDs->GetTypedValue(0.0f);
        }

        // Face vertex indices
        if (auto fviDs =
                HdTypedSampledDataSource<VtArray<int>>::Cast(
                    meshDs->Get(_tokens->faceVertexIndices))) {
            mesh.faceVertexIndices = fviDs->GetTypedValue(0.0f);
        }

        // Normals (optional)
        if (auto normDs =
                HdTypedSampledDataSource<VtArray<GfVec3f>>::Cast(
                    meshDs->Get(_tokens->normals))) {
            mesh.normals = normDs->GetTypedValue(0.0f);
        }

        if (!mesh.points.empty() && !mesh.faceVertexIndices.empty()) {
            _meshes.push_back(std::move(mesh));
        }
    }

    // Unpack B-rep edge polylines (edges_<i> containers) into linear-curve data.
    int edgeCount = 0;
    if (auto ecDs = HdTypedSampledDataSource<int>::Cast(
            tessDs->Get(_tokens->edgeCount))) {
        edgeCount = ecDs->GetTypedValue(0.0f);
    }
    for (int i = 0; i < edgeCount; ++i) {
        TfToken edgeName(TfStringPrintf("edges_%d", i));
        HdContainerDataSourceHandle edgeDs =
            HdContainerDataSource::Cast(tessDs->Get(edgeName));
        if (!edgeDs) {
            continue;  // this Brep produced no drawable edges
        }
        _EdgeData edge;
        if (auto pDs = HdTypedSampledDataSource<VtArray<GfVec3f>>::Cast(
                edgeDs->Get(_tokens->points))) {
            edge.points = pDs->GetTypedValue(0.0f);
        }
        if (auto cvcDs = HdTypedSampledDataSource<VtArray<int>>::Cast(
                edgeDs->Get(_tokens->curveVertexCounts))) {
            edge.curveVertexCounts = cvcDs->GetTypedValue(0.0f);
        }
        if (!edge.points.empty() && !edge.curveVertexCounts.empty()) {
            _edges.push_back(std::move(edge));
        }
    }

    // Unpack tangent (smooth) edge polylines (tangent_edges_<i> containers).
    int tangentEdgeCount = 0;
    if (auto tcDs = HdTypedSampledDataSource<int>::Cast(
            tessDs->Get(_tokens->tangentEdgeCount))) {
        tangentEdgeCount = tcDs->GetTypedValue(0.0f);
    }
    for (int i = 0; i < tangentEdgeCount; ++i) {
        TfToken name(TfStringPrintf("tangent_edges_%d", i));
        HdContainerDataSourceHandle tDs =
            HdContainerDataSource::Cast(tessDs->Get(name));
        if (!tDs) {
            continue;
        }
        _EdgeData edge;
        if (auto pDs = HdTypedSampledDataSource<VtArray<GfVec3f>>::Cast(
                tDs->Get(_tokens->points))) {
            edge.points = pDs->GetTypedValue(0.0f);
        }
        if (auto cvcDs = HdTypedSampledDataSource<VtArray<int>>::Cast(
                tDs->Get(_tokens->curveVertexCounts))) {
            edge.curveVertexCounts = cvcDs->GetTypedValue(0.0f);
        }
        if (!edge.points.empty() && !edge.curveVertexCounts.empty()) {
            _tangentEdges.push_back(std::move(edge));
        }
    }
}

HdGpGenerativeProcedural::ChildPrimTypeMap
UsdSolidTessellationProcedural::Update(
    const HdSceneIndexBaseRefPtr &inputScene,
    const ChildPrimTypeMap &previousResult,
    const DependencyMap &dirtiedDependencies,
    HdSceneIndexObserver::DirtiedPrimEntries *outputDirtiedPrims)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_cooked || !dirtiedDependencies.empty()) {
        _Tessellate(inputScene);
        _cooked = true;
    }

    ChildPrimTypeMap result;
    for (size_t i = 0; i < _meshes.size(); ++i) {
        SdfPath childPath = _GetProceduralPrimPath().AppendChild(
            TfToken(TfStringPrintf("tessellated_mesh_%zu", i)));
        result[childPath] = HdPrimTypeTokens->mesh;
    }
    for (size_t i = 0; i < _edges.size(); ++i) {
        SdfPath childPath = _GetProceduralPrimPath().AppendChild(
            TfToken(TfStringPrintf("tessellated_edges_%zu", i)));
        result[childPath] = HdPrimTypeTokens->basisCurves;
    }
    for (size_t i = 0; i < _tangentEdges.size(); ++i) {
        SdfPath childPath = _GetProceduralPrimPath().AppendChild(
            TfToken(TfStringPrintf("tessellated_tangent_edges_%zu", i)));
        result[childPath] = HdPrimTypeTokens->basisCurves;
    }

    // If previous results existed, dirty all children
    if (!previousResult.empty() && outputDirtiedPrims) {
        for (const auto &[path, type] : result) {
            outputDirtiedPrims->emplace_back(
                path, HdDataSourceLocatorSet::UniversalSet());
        }
    }

    return result;
}

HdSceneIndexPrim
UsdSolidTessellationProcedural::GetChildPrim(
    const HdSceneIndexBaseRefPtr &inputScene,
    const SdfPath &childPrimPath)
{
    std::lock_guard<std::mutex> lock(_mutex);

    const std::string childName = childPrimPath.GetName();

    // B-rep edge polylines -> a linear HdBasisCurves child, marked purpose=guide
    // so usdview's "Display > Guides" toggles the edge overlay like a wireframe.
    // Sharp/feature edges (dark) and tangent/smooth edges (lighter) are emitted
    // as separate child sets so tangent edges can be toggled independently.
    const std::string tanPrefix = "tessellated_tangent_edges_";
    const std::string edgePrefix = "tessellated_edges_";
    const bool isTangent =
        childName.compare(0, tanPrefix.size(), tanPrefix) == 0;
    const bool isSharp = !isTangent &&
        childName.compare(0, edgePrefix.size(), edgePrefix) == 0;
    if (isSharp || isTangent) {
        HdSceneIndexPrim cresult;
        cresult.primType = HdPrimTypeTokens->basisCurves;
        const std::vector<_EdgeData> &edgeSet =
            isTangent ? _tangentEdges : _edges;
        const std::string &pfx = isTangent ? tanPrefix : edgePrefix;
        size_t edgeIdx = 0;
        try {
            edgeIdx = std::stoul(childName.substr(pfx.size()));
        } catch (...) {
            return cresult;
        }
        if (edgeIdx >= edgeSet.size()) {
            return cresult;
        }
        const _EdgeData &edge = edgeSet[edgeIdx];

        HdContainerDataSourceHandle curveTopo =
            HdBasisCurvesTopologySchema::Builder()
                .SetCurveVertexCounts(
                    HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                        edge.curveVertexCounts))
                .SetType(HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdTokens->linear))
                .SetWrap(HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdTokens->nonperiodic))
                .Build();
        HdContainerDataSourceHandle curvesDs =
            HdBasisCurvesSchema::Builder()
                .SetTopology(curveTopo)
                .Build();

        HdContainerDataSourceHandle pointsPvDs =
            HdRetainedContainerDataSource::New(
                HdPrimvarSchemaTokens->primvarValue,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    edge.points),
                HdPrimvarSchemaTokens->interpolation,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdPrimvarSchemaTokens->vertex),
                HdPrimvarSchemaTokens->role,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdPrimvarSchemaTokens->point));
        VtArray<GfVec3f> edgeColor(1, isTangent
            ? GfVec3f(0.5f, 0.5f, 0.55f) : GfVec3f(0.05f, 0.05f, 0.05f));
        HdContainerDataSourceHandle colorPvDs =
            HdRetainedContainerDataSource::New(
                HdPrimvarSchemaTokens->primvarValue,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    edgeColor),
                HdPrimvarSchemaTokens->interpolation,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdPrimvarSchemaTokens->constant),
                HdPrimvarSchemaTokens->role,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdPrimvarSchemaTokens->color));
        HdContainerDataSourceHandle primvarsDs =
            HdRetainedContainerDataSource::New(
                HdTokens->points, pointsPvDs,
                HdTokens->displayColor, colorPvDs);

        HdContainerDataSourceHandle xformDs;
        if (inputScene) {
            const HdSceneIndexPrim procPrim =
                inputScene->GetPrim(_GetProceduralPrimPath());
            if (procPrim.dataSource) {
                if (HdXformSchema xs =
                        HdXformSchema::GetFromParent(procPrim.dataSource)) {
                    xformDs = xs.GetContainer();
                }
            }
        }
        HdContainerDataSourceHandle purposeDs =
            HdPurposeSchema::Builder()
                .SetPurpose(HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdRenderTagTokens->guide))
                .Build();

        if (xformDs) {
            cresult.dataSource = HdRetainedContainerDataSource::New(
                HdBasisCurvesSchemaTokens->basisCurves, curvesDs,
                HdPrimvarsSchemaTokens->primvars, primvarsDs,
                HdXformSchemaTokens->xform, xformDs,
                HdPurposeSchemaTokens->purpose, purposeDs);
        } else {
            cresult.dataSource = HdRetainedContainerDataSource::New(
                HdBasisCurvesSchemaTokens->basisCurves, curvesDs,
                HdPrimvarsSchemaTokens->primvars, primvarsDs,
                HdPurposeSchemaTokens->purpose, purposeDs);
        }
        return cresult;
    }

    HdSceneIndexPrim result;
    result.primType = HdPrimTypeTokens->mesh;

    // Parse mesh index from child name
    const std::string prefix = "tessellated_mesh_";
    size_t meshIdx = 0;
    if (childName.substr(0, prefix.size()) != prefix) {
        return result;
    }
    try {
        meshIdx = std::stoul(childName.substr(prefix.size()));
    } catch (...) {
        return result;
    }
    if (meshIdx >= _meshes.size()) {
        return result;
    }

    const _MeshData &mesh = _meshes[meshIdx];

    // Build mesh topology
    HdContainerDataSourceHandle topologyDs =
        HdMeshTopologySchema::Builder()
            .SetFaceVertexCounts(
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    mesh.faceVertexCounts))
            .SetFaceVertexIndices(
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    mesh.faceVertexIndices))
            .Build();

    HdContainerDataSourceHandle meshDs =
        HdMeshSchema::Builder()
            .SetTopology(topologyDs)
            // Tessellated output is a literal triangle mesh; pin the scheme to
            // "none" so Storm never Catmull-Clark-subdivides it at higher
            // complexity (which would also discard the authored normals).
            .SetSubdivisionScheme(
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    PxOsdOpenSubdivTokens->none))
            .Build();

    // Points primvar
    HdContainerDataSourceHandle pointsPvDs =
        HdRetainedContainerDataSource::New(
            HdPrimvarSchemaTokens->primvarValue,
            HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                mesh.points),
            HdPrimvarSchemaTokens->interpolation,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                HdPrimvarSchemaTokens->vertex),
            HdPrimvarSchemaTokens->role,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                HdPrimvarSchemaTokens->point));

    // displayColor for Storm shading: use the source BrepArray prim's authored
    // constant primvars:displayColor when present (e.g. per-part colors from a
    // CAD assembly conversion); fall back to the bright-metallic-grey default.
    VtArray<GfVec3f> displayColor(1, GfVec3f(0.85f, 0.87f, 0.9f));
    if (inputScene) {
        const HdSceneIndexPrim srcPrim =
            inputScene->GetPrim(_GetProceduralPrimPath());
        if (srcPrim.dataSource) {
            if (HdPrimvarsSchema pvs =
                    HdPrimvarsSchema::GetFromParent(srcPrim.dataSource)) {
                if (HdPrimvarSchema pv =
                        pvs.GetPrimvar(HdTokens->displayColor)) {
                    if (HdSampledDataSourceHandle vds = pv.GetPrimvarValue()) {
                        const VtValue v = vds->GetValue(0.0f);
                        if (v.IsHolding<VtArray<GfVec3f>>()) {
                            const VtArray<GfVec3f> authored =
                                v.UncheckedGet<VtArray<GfVec3f>>();
                            if (!authored.empty()) {
                                // Constant interpolation on the output mesh:
                                // take the first (dominant) authored color.
                                displayColor =
                                    VtArray<GfVec3f>(1, authored[0]);
                            }
                        }
                    }
                }
            }
        }
    }
    HdContainerDataSourceHandle displayColorPvDs =
        HdRetainedContainerDataSource::New(
            HdPrimvarSchemaTokens->primvarValue,
            HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                displayColor),
            HdPrimvarSchemaTokens->interpolation,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                HdPrimvarSchemaTokens->constant),
            HdPrimvarSchemaTokens->role,
            HdRetainedTypedSampledDataSource<TfToken>::New(
                HdPrimvarSchemaTokens->color));

    HdContainerDataSourceHandle primvarsDs;
    if (!mesh.normals.empty()) {
        HdContainerDataSourceHandle normalsPvDs =
            HdRetainedContainerDataSource::New(
                HdPrimvarSchemaTokens->primvarValue,
                HdRetainedTypedSampledDataSource<VtArray<GfVec3f>>::New(
                    mesh.normals),
                HdPrimvarSchemaTokens->interpolation,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdPrimvarSchemaTokens->vertex),
                HdPrimvarSchemaTokens->role,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    HdPrimvarSchemaTokens->normal));

        primvarsDs = HdRetainedContainerDataSource::New(
            HdTokens->points, pointsPvDs,
            HdTokens->normals, normalsPvDs,
            HdTokens->displayColor, displayColorPvDs);
    } else {
        primvarsDs = HdRetainedContainerDataSource::New(
            HdTokens->points, pointsPvDs,
            HdTokens->displayColor, displayColorPvDs);
    }

    // Generated meshes must carry the source BrepArray prim's transform;
    // xform flattening runs before hdGp expansion, so children do not
    // inherit it implicitly. Without this, every tessellated mesh renders
    // at the world origin regardless of authored Xforms.
    HdContainerDataSourceHandle xformDs;
    if (inputScene) {
        const HdSceneIndexPrim procPrim =
            inputScene->GetPrim(_GetProceduralPrimPath());
        if (procPrim.dataSource) {
            if (HdXformSchema xformSchema =
                    HdXformSchema::GetFromParent(procPrim.dataSource)) {
                xformDs = xformSchema.GetContainer();
            }
        }
    }

    if (xformDs) {
        result.dataSource = HdRetainedContainerDataSource::New(
            HdMeshSchemaTokens->mesh, meshDs,
            HdPrimvarsSchemaTokens->primvars, primvarsDs,
            HdXformSchemaTokens->xform, xformDs);
    } else {
        result.dataSource = HdRetainedContainerDataSource::New(
            HdMeshSchemaTokens->mesh, meshDs,
            HdPrimvarsSchemaTokens->primvars, primvarsDs);
    }

    return result;
}

// --------------------------------------------------------------------------
// UsdSolidTessellationProceduralPlugin
// --------------------------------------------------------------------------

UsdSolidTessellationProceduralPlugin::UsdSolidTessellationProceduralPlugin()
    = default;

UsdSolidTessellationProceduralPlugin::~UsdSolidTessellationProceduralPlugin()
    = default;

HdGpGenerativeProcedural *
UsdSolidTessellationProceduralPlugin::Construct(
    const SdfPath &proceduralPrimPath)
{
    return new UsdSolidTessellationProcedural(proceduralPrimPath);
}

// --------------------------------------------------------------------------
// Plugin Registration
// --------------------------------------------------------------------------

TF_REGISTRY_FUNCTION(TfType)
{
    HdGpGenerativeProceduralPluginRegistry::Define<
        UsdSolidTessellationProceduralPlugin,
        HdGpGenerativeProceduralPlugin>();
}

PXR_NAMESPACE_CLOSE_SCOPE

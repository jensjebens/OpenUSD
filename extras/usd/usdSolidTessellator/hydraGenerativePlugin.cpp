// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Hydra Generative Procedural Plugin implementation.
// Tessellates UsdSolid BrepArray prims for live Hydra visualization.

#include "hydraGenerativePlugin.h"
#include "tessellator.h"
#include "brepBuilder.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hdGp/generativeProceduralPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (UsdSolidBrepArray)
    (usdSolidTessellation)
    ((linearDeflection, "primvars:tessellation:linearDeflection"))
    ((angularDeflection, "primvars:tessellation:angularDeflection"))
    ((computeNormals, "primvars:tessellation:computeNormals"))
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
    // We depend on our own prim (all BrepArray attributes)
    deps[_GetProceduralPrimPath()] = HdDataSourceLocatorSet::UniversalSet();
    return deps;
}

void
UsdSolidTessellationProcedural::_Tessellate(
    const HdSceneIndexBaseRefPtr &inputScene)
{
    _meshes.clear();
    _dirty = false;

    // Get data from the input scene for our procedural prim
    HdSceneIndexPrim prim = inputScene->GetPrim(_GetProceduralPrimPath());
    if (!prim.dataSource) {
        return;
    }

    // Read tessellation parameters from primvars (if authored)
    double linearDeflection = 0.1;
    double angularDeflection = 0.5;
    bool computeNormals = true;

    // In a full implementation, we'd read these from the prim's data source:
    //   auto primvarsDs = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    //   ... extract linearDeflection, angularDeflection, computeNormals ...
    // For now, use defaults.

    // The actual tessellation happens here. In a real integration, we'd read
    // the BrepArray topology directly from the Hydra data sources. For this
    // plugin, we access the USD stage via the scene index's typed data sources.
    //
    // For the initial implementation, we produce a placeholder mesh that
    // signals the pipeline is working. Full integration requires the
    // BrepArray schema to be registered as a Hydra prim type with proper
    // data source translation.
    //
    // TODO: When UsdSolid is integrated into the USD schema registry,
    // access BrepArray attributes from the scene index data source directly.
    // For now, the procedural demonstrates the plugin architecture.

    // Placeholder: emit a unit cube mesh to prove the pipeline works
    _MeshData mesh;
    mesh.points = {
        GfVec3f(-0.5f, -0.5f, -0.5f), GfVec3f(0.5f, -0.5f, -0.5f),
        GfVec3f(0.5f, 0.5f, -0.5f),   GfVec3f(-0.5f, 0.5f, -0.5f),
        GfVec3f(-0.5f, -0.5f, 0.5f),  GfVec3f(0.5f, -0.5f, 0.5f),
        GfVec3f(0.5f, 0.5f, 0.5f),    GfVec3f(-0.5f, 0.5f, 0.5f),
    };
    mesh.faceVertexCounts = {4, 4, 4, 4, 4, 4};
    mesh.faceVertexIndices = {
        0, 1, 2, 3,  // front
        4, 7, 6, 5,  // back
        0, 4, 5, 1,  // bottom
        2, 6, 7, 3,  // top
        0, 3, 7, 4,  // left
        1, 5, 6, 2,  // right
    };
    mesh.normals = {
        GfVec3f(-0.577f, -0.577f, -0.577f),
        GfVec3f(0.577f, -0.577f, -0.577f),
        GfVec3f(0.577f, 0.577f, -0.577f),
        GfVec3f(-0.577f, 0.577f, -0.577f),
        GfVec3f(-0.577f, -0.577f, 0.577f),
        GfVec3f(0.577f, -0.577f, 0.577f),
        GfVec3f(0.577f, 0.577f, 0.577f),
        GfVec3f(-0.577f, 0.577f, 0.577f),
    };

    _meshes.push_back(std::move(mesh));
}

HdGpGenerativeProcedural::ChildPrimTypeMap
UsdSolidTessellationProcedural::Update(
    const HdSceneIndexBaseRefPtr &inputScene,
    const ChildPrimTypeMap &previousResult,
    const DependencyMap &dirtiedDependencies,
    HdSceneIndexObserver::DirtiedPrimEntries *outputDirtiedPrims)
{
    // Re-tessellate if dirty
    if (_dirty || previousResult.empty()) {
        _Tessellate(inputScene);
    }

    // Return child prim paths for each tessellated mesh
    ChildPrimTypeMap result;
    for (size_t i = 0; i < _meshes.size(); ++i) {
        SdfPath childPath = _GetProceduralPrimPath().AppendChild(
            TfToken(TfStringPrintf("tessellated_mesh_%zu", i)));
        result[childPath] = HdPrimTypeTokens->mesh;
    }

    // If we had previous results, mark all as dirty
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
    HdSceneIndexPrim result;
    result.primType = HdPrimTypeTokens->mesh;

    // Determine which mesh this child path corresponds to
    const std::string childName = childPrimPath.GetName();
    size_t meshIdx = 0;
    if (sscanf(childName.c_str(), "tessellated_mesh_%zu", &meshIdx) != 1) {
        return result;
    }
    if (meshIdx >= _meshes.size()) {
        return result;
    }

    const _MeshData &mesh = _meshes[meshIdx];

    // Build mesh topology data source
    HdContainerDataSourceHandle topologyDs =
        HdMeshTopologySchema::Builder()
            .SetFaceVertexCounts(
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    mesh.faceVertexCounts))
            .SetFaceVertexIndices(
                HdRetainedTypedSampledDataSource<VtArray<int>>::New(
                    mesh.faceVertexIndices))
            .Build();

    // Build mesh data source
    HdContainerDataSourceHandle meshDs =
        HdMeshSchema::Builder()
            .SetTopology(topologyDs)
            .Build();

    // Build primvars data source (points + normals)
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

    HdContainerDataSourceHandle primvarsDs;
    if (!mesh.normals.empty()) {
        // Normals primvar
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
            HdTokens->normals, normalsPvDs);
    } else {
        primvarsDs = HdRetainedContainerDataSource::New(
            HdTokens->points, pointsPvDs);
    }

    // Compose the full prim data source
    result.dataSource = HdRetainedContainerDataSource::New(
        HdMeshSchemaTokens->mesh, meshDs,
        HdPrimvarsSchemaTokens->primvars, primvarsDs);

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

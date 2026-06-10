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
    (points)
    (faceVertexCounts)
    (faceVertexIndices)
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

    TF_STATUS("UsdSolidTessellationProcedural: Read %zu mesh(es) for '%s' "
              "(%zu total verts)",
              _meshes.size(), primPath.GetText(),
              [&]() {
                  size_t total = 0;
                  for (const auto& m : _meshes)
                      total += m.points.size();
                  return total;
              }());
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

    HdSceneIndexPrim result;
    result.primType = HdPrimTypeTokens->mesh;

    // Parse mesh index from child name
    const std::string childName = childPrimPath.GetName();
    size_t meshIdx = 0;
    if (sscanf(childName.c_str(), "tessellated_mesh_%zu", &meshIdx) != 1) {
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

        // displayColor for Storm shading (bright metallic grey)
        VtArray<GfVec3f> displayColor(1, GfVec3f(0.85f, 0.87f, 0.9f));
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

        primvarsDs = HdRetainedContainerDataSource::New(
            HdTokens->points, pointsPvDs,
            HdTokens->normals, normalsPvDs,
            HdTokens->displayColor, displayColorPvDs);
    } else {
        // displayColor for Storm shading (bright metallic grey)
        VtArray<GfVec3f> displayColor(1, GfVec3f(0.85f, 0.87f, 0.9f));
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

        primvarsDs = HdRetainedContainerDataSource::New(
            HdTokens->points, pointsPvDs,
            HdTokens->displayColor, displayColorPvDs);
    }

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

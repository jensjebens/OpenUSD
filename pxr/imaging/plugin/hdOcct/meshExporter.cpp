// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// MeshExporter implementation: writes tessellation results to UsdStage.

#include "meshExporter.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/primvarsAPI.h"
#include "pxr/usd/usdGeom/subset.h"
#include "pxr/usd/usdGeom/tokens.h"

#include <set>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

UsdSolidMeshExporter::UsdSolidMeshExporter() = default;
UsdSolidMeshExporter::~UsdSolidMeshExporter() = default;

SdfPath
UsdSolidMeshExporter::Export(
    const UsdStageRefPtr& stage,
    const SdfPath& meshPath,
    const UsdSolidTessellationResult& result) const
{
    if (!result.success || result.points.empty()) {
        TF_WARN("Cannot export empty or failed tessellation result to '%s'",
                 meshPath.GetText());
        return SdfPath();
    }

    // Create the mesh prim
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, meshPath);
    if (!mesh) {
        TF_WARN("Failed to define UsdGeomMesh at '%s'", meshPath.GetText());
        return SdfPath();
    }

    // Expects pre-compacted result (call result.Compact() before Export).
    // Points: convert double -> float for USD mesh
    VtArray<GfVec3f> pointsF(result.points.size());
    for (size_t i = 0; i < result.points.size(); ++i) {
        pointsF[i] = GfVec3f(result.points[i]);
    }
    mesh.GetPointsAttr().Set(pointsF);

    // Face vertex counts and indices (already compacted)
    mesh.GetFaceVertexCountsAttr().Set(result.faceVertexCounts);
    mesh.GetFaceVertexIndicesAttr().Set(result.faceVertexIndices);

    // Subdivision scheme = none (triangle mesh)
    mesh.GetSubdivisionSchemeAttr().Set(subdivisionScheme);

    // Normals as primvar (vertex interpolation — already compacted)
    if (!result.normals.empty()) {
        UsdGeomPrimvarsAPI primvarsAPI(mesh);
        auto normalsPv = primvarsAPI.CreatePrimvar(
            TfToken("normals"),
            SdfValueTypeNames->Float3Array,
            UsdGeomTokens->vertex);
        normalsPv.Set(result.normals);
    }

    // UVs as primvar (vertex interpolation — already compacted)
    if (!result.uvs.empty()) {
        UsdGeomPrimvarsAPI primvarsAPI(mesh);
        auto uvPv = primvarsAPI.CreatePrimvar(
            TfToken("st"),
            SdfValueTypeNames->Float2Array,
            UsdGeomTokens->vertex);
        uvPv.Set(result.uvs);
    }

    // Compute and set extent
    if (computeExtent) {
        GfVec3f minPt(std::numeric_limits<float>::max());
        GfVec3f maxPt(std::numeric_limits<float>::lowest());
        for (const auto& p : pointsF) {
            for (int i = 0; i < 3; ++i) {
                minPt[i] = std::min(minPt[i], p[i]);
                maxPt[i] = std::max(maxPt[i], p[i]);
            }
        }
        VtArray<GfVec3f> extent = {minPt, maxPt};
        mesh.GetExtentAttr().Set(extent);
    }

    // Create GeomSubsets for per-face material assignment
    if (createGeomSubsets && !result.faceSolidFaceIndices.empty()) {
        // Group triangle indices by solid face index
        std::map<int, VtArray<int>> faceGroups;
        for (size_t i = 0; i < result.faceSolidFaceIndices.size(); ++i) {
            faceGroups[result.faceSolidFaceIndices[i]].push_back((int)i);
        }

        if (faceGroups.size() > 1) {
            for (const auto& [solidFaceIdx, triIndices] : faceGroups) {
                std::string subsetName = TfStringPrintf("face_%d", solidFaceIdx);
                SdfPath subsetPath = meshPath.AppendChild(
                    TfToken(subsetName));
                UsdGeomSubset subset = UsdGeomSubset::Define(stage, subsetPath);
                subset.GetIndicesAttr().Set(triIndices);
                subset.GetElementTypeAttr().Set(TfToken("face"));
                subset.GetFamilyNameAttr().Set(TfToken("materialBind"));
            }
        }
    }

    return meshPath;
}

std::vector<SdfPath>
UsdSolidMeshExporter::ExportAll(
    const UsdStageRefPtr& stage,
    const SdfPath& parentPath,
    const std::vector<UsdSolidTessellationResult>& results) const
{
    std::vector<SdfPath> paths;
    paths.reserve(results.size());

    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].success) continue;

        std::string meshName = TfStringPrintf("mesh_%zu", i);
        SdfPath meshPath = parentPath.AppendChild(TfToken(meshName));
        SdfPath created = Export(stage, meshPath, results[i]);
        if (!created.IsEmpty()) {
            paths.push_back(created);
        }
    }

    return paths;
}

PXR_NAMESPACE_CLOSE_SCOPE

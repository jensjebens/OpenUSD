// C-callable mesh export for use via ctypes/Python and the CLI tool.
#include "api.h"
#include "tessellator.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/vt/array.h"
#include <set>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Write one tessellation result as a Mesh prim (compacted vertices, literal
// triangle mesh). Returns false when the result carries no geometry.
bool _WriteMeshPrim(
    const UsdStageRefPtr& outStage,
    const SdfPath& meshPath,
    const UsdSolidTessellationResult& result,
    const GfVec3f& displayColor)
{
    if (result.points.empty()) return false;

    auto mesh = UsdGeomMesh::Define(outStage, meshPath);

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

    VtArray<GfVec3f> points3f(newIdx);
    for (size_t j = 0; j < result.points.size(); ++j) {
        if (oldToNew[j] >= 0) {
            const auto& p = result.points[j];
            points3f[oldToNew[j]] =
                GfVec3f((float)p[0], (float)p[1], (float)p[2]);
        }
    }

    VtArray<int> remappedIndices(result.faceVertexIndices.size());
    for (size_t j = 0; j < result.faceVertexIndices.size(); ++j) {
        remappedIndices[j] = oldToNew[result.faceVertexIndices[j]];
    }

    mesh.GetPointsAttr().Set(points3f);
    mesh.GetFaceVertexCountsAttr().Set(result.faceVertexCounts);
    mesh.GetFaceVertexIndicesAttr().Set(remappedIndices);
    // Tessellated output is a literal triangle mesh: without this the
    // USD default (catmullClark) would subdivide it and renderers
    // would ignore the authored normals.
    mesh.GetSubdivisionSchemeAttr().Set(UsdGeomTokens->none);

    if (!result.normals.empty()) {
        VtArray<GfVec3f> compactNormals(newIdx);
        for (size_t j = 0; j < result.normals.size()
             && j < result.points.size(); ++j) {
            if (oldToNew[j] >= 0) {
                compactNormals[oldToNew[j]] = result.normals[j];
            }
        }
        mesh.GetNormalsAttr().Set(compactNormals);
        mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
    }

    VtArray<GfVec3f> colors(1, displayColor);
    mesh.GetDisplayColorAttr().Set(colors);
    return true;
}

// The body's constant display color: the parent Xform's authored
// primvars:displayColor (single entry), falling back to a neutral grey.
// The per-FACE color array some producers author on the BrepArray prim
// itself is deliberately not consulted here -- a constant is wanted.
GfVec3f _BodyDisplayColor(const UsdPrim& brepPrim)
{
    UsdPrim parent = brepPrim.GetParent();
    if (parent) {
        UsdAttribute attr =
            parent.GetAttribute(TfToken("primvars:displayColor"));
        VtArray<GfVec3f> v;
        if (attr && attr.Get(&v) && !v.empty()) return v[0];
    }
    return GfVec3f(0.85f, 0.87f, 0.9f);
}

} // anonymous namespace

extern "C" {

HDOCCT_API
int UsdSolid_ExportMesh(const char* inputPath, const char* outputPath, const char* primPath) {
    auto inputStage = UsdStage::Open(inputPath);
    if (!inputStage) return -1;

    UsdSolidTessellator tessellator;
    UsdSolidTessellationParams params;
    params.linearDeflection = 0.1;
    params.angularDeflection = 0.5;
    params.computeNormals = true;
    params.computeUVs = true;

    // All-bodies mode: an empty/"-"/"--all" prim path bakes EVERY BrepArray
    // prim on the stage into one Mesh stage -- one mesh per body, named
    // after the body's parent prim, carrying the body's displayColor and
    // the source stage's upAxis/metersPerUnit.
    const bool allBodies =
        (primPath == nullptr || primPath[0] == '\0'
         || std::strcmp(primPath, "-") == 0
         || std::strcmp(primPath, "--all") == 0);

    if (allBodies) {
        auto outStage = UsdStage::CreateNew(outputPath);
        VtValue up, mpu;
        if (inputStage->GetMetadata(UsdGeomTokens->upAxis, &up))
            outStage->SetMetadata(UsdGeomTokens->upAxis, up);
        if (inputStage->GetMetadata(UsdGeomTokens->metersPerUnit, &mpu))
            outStage->SetMetadata(UsdGeomTokens->metersPerUnit, mpu);
        auto world = UsdGeomXform::Define(outStage, SdfPath("/World"));
        outStage->SetDefaultPrim(world.GetPrim());

        int meshCount = 0;
        int totalVerts = 0;
        std::set<std::string> used;
        for (const UsdPrim& prim : inputStage->Traverse()) {
            if (prim.GetTypeName() != TfToken("BrepArray")) continue;
            const UsdPrim parent = prim.GetParent();
            std::string base = parent && parent.GetPath() != SdfPath("/")
                ? parent.GetName().GetString()
                : prim.GetName().GetString();
            std::string name = base;
            for (int k = 1; !used.insert(name).second; ++k) {
                name = base + "_" + std::to_string(k);
            }
            const GfVec3f color = _BodyDisplayColor(prim);
            auto results = tessellator.Tessellate(prim, params);
            int bodyMeshes = 0;
            for (size_t i = 0; i < results.size(); ++i) {
                const std::string leaf = results.size() == 1
                    ? name : name + "_brep" + std::to_string(i);
                if (_WriteMeshPrim(outStage,
                        SdfPath("/World/" + leaf), results[i], color)) {
                    ++bodyMeshes;
                    ++meshCount;
                    totalVerts += (int)results[i].points.size();
                }
            }
            fprintf(stdout, "  %s: %d mesh(es)\n", name.c_str(), bodyMeshes);
        }
        outStage->Save();
        fprintf(stdout, "Wrote %d meshes (%d total verts) to %s\n",
                meshCount, totalVerts, outputPath);
        return meshCount;
    }

    UsdPrim brepPrim = inputStage->GetPrimAtPath(SdfPath(primPath));
    if (!brepPrim) return -2;

    auto results = tessellator.Tessellate(brepPrim, params);
    fprintf(stdout, "Tessellated %zu bodies\n", results.size());

    auto outStage = UsdStage::CreateNew(outputPath);
    outStage->SetMetadata(UsdGeomTokens->upAxis, VtValue(TfToken("Y")));
    auto world = UsdGeomXform::Define(outStage, SdfPath("/World"));
    outStage->SetDefaultPrim(world.GetPrim());

    int meshCount = 0;
    int totalVerts = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        std::string meshPathStr = "/World/mesh_" + std::to_string(i);
        if (_WriteMeshPrim(outStage, SdfPath(meshPathStr), results[i],
                           GfVec3f(0.85f, 0.87f, 0.9f))) {
            meshCount++;
            totalVerts += (int)results[i].points.size();
        }
    }

    outStage->Save();
    fprintf(stdout, "Wrote %d meshes (%d total verts) to %s\n",
            meshCount, totalVerts, outputPath);
    return meshCount;
}

} // extern "C"

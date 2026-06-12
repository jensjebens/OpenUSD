// C-callable mesh export for use via ctypes/Python and the CLI tool.
#include "api.h"
#include "tessellator.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/vt/array.h"
#include <string>
#include <vector>
#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

extern "C" {

HDOCCT_API
int UsdSolid_ExportMesh(const char* inputPath, const char* outputPath, const char* primPath) {
    auto inputStage = UsdStage::Open(inputPath);
    if (!inputStage) return -1;

    UsdPrim brepPrim = inputStage->GetPrimAtPath(SdfPath(primPath));
    if (!brepPrim) return -2;

    UsdSolidTessellator tessellator;
    UsdSolidTessellationParams params;
    params.linearDeflection = 0.1;
    params.angularDeflection = 0.5;
    params.computeNormals = true;
    params.computeUVs = true;

    auto results = tessellator.Tessellate(brepPrim, params);
    fprintf(stdout, "Tessellated %zu bodies\n", results.size());

    auto outStage = UsdStage::CreateNew(outputPath);
    outStage->SetMetadata(UsdGeomTokens->upAxis, VtValue(TfToken("Y")));
    auto world = UsdGeomXform::Define(outStage, SdfPath("/World"));
    outStage->SetDefaultPrim(world.GetPrim());

    int meshCount = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        if (result.points.empty()) continue;

        std::string meshPathStr = "/World/mesh_" + std::to_string(i);
        auto mesh = UsdGeomMesh::Define(outStage, SdfPath(meshPathStr));

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
                points3f[oldToNew[j]] = GfVec3f((float)p[0], (float)p[1], (float)p[2]);
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

        VtArray<GfVec3f> colors(1, GfVec3f(0.85f, 0.87f, 0.9f));
        mesh.GetDisplayColorAttr().Set(colors);
        meshCount++;
    }

    outStage->Save();
    int totalVerts = 0;
    for (const auto& r : results) totalVerts += r.points.size();
    fprintf(stdout, "Wrote %d meshes (%d total verts) to %s\n",
            meshCount, totalVerts, outputPath);
    return meshCount;
}

} // extern "C"

// C-callable mesh export for use via ctypes/Python and the CLI tool.
#include "api.h"
#include "tessellator.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/subset.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/vt/array.h"
#include <map>
#include <string>
#include <vector>
#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Shared implementation. Takes the full tessellation params so both the legacy
// fixed-quality ABI (UsdSolid_ExportMesh) and the quality-aware ABI
// (UsdSolid_ExportMeshEx used by the CLI --*-deflection flags) route through one
// body. The per-face GeomSubset authoring (PR #62 feedback, agent 09) lives here
// so both entry points emit the same face_<i> subsets regardless of density.
int ExportMeshImpl(const char* inputPath, const char* outputPath,
                   const char* primPath,
                   const UsdSolidTessellationParams& params) {
    auto inputStage = UsdStage::Open(inputPath);
    if (!inputStage) return -1;

    UsdPrim brepPrim = inputStage->GetPrimAtPath(SdfPath(primPath));
    if (!brepPrim) return -2;

    UsdSolidTessellator tessellator;

    auto results = tessellator.Tessellate(brepPrim, params);
    fprintf(stdout,
            "Tessellated %zu bodies (linearDeflection=%g angularDeflection=%g "
            "relative=%d)\n",
            results.size(), params.linearDeflection, params.angularDeflection,
            params.relativeDeflection ? 1 : 0);

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

        // Per-face GeomSubsets so downstream consumers can map each triangle
        // back to the B-rep face it came from (highlight/pick, material bind).
        // Mirrors UsdSolidMeshExporter::Export: group the per-triangle
        // faceSolidFaceIndices into face_<solidFaceIdx> subsets. The vertex
        // compaction above only remaps point indices, not triangle order, so
        // faceSolidFaceIndices[t] still names triangle t of the authored mesh.
        if (!result.faceSolidFaceIndices.empty()) {
            std::map<int, VtArray<int>> faceGroups;
            for (size_t t = 0; t < result.faceSolidFaceIndices.size(); ++t) {
                faceGroups[result.faceSolidFaceIndices[t]].push_back((int)t);
            }
            if (faceGroups.size() > 1) {
                for (const auto& kv : faceGroups) {
                    std::string subsetName =
                        TfStringPrintf("face_%d", kv.first);
                    SdfPath subsetPath =
                        mesh.GetPath().AppendChild(TfToken(subsetName));
                    UsdGeomSubset subset =
                        UsdGeomSubset::Define(outStage, subsetPath);
                    subset.GetIndicesAttr().Set(kv.second);
                    subset.GetElementTypeAttr().Set(TfToken("face"));
                    subset.GetFamilyNameAttr().Set(TfToken("materialBind"));
                }
            }
        }
        meshCount++;
    }

    outStage->Save();
    int totalVerts = 0;
    for (const auto& r : results) totalVerts += r.points.size();
    fprintf(stdout, "Wrote %d meshes (%d total verts) to %s\n",
            meshCount, totalVerts, outputPath);
    return meshCount;
}

}  // namespace

extern "C" {

// Legacy fixed-quality ABI (kept stable for existing callers). Uses the coarse
// defaults that shipped in the header (linearDeflection 0.1, angularDeflection
// 0.5 rad); the CLI now passes finer values through UsdSolid_ExportMeshEx.
HDOCCT_API
int UsdSolid_ExportMesh(const char* inputPath, const char* outputPath, const char* primPath) {
    UsdSolidTessellationParams params;
    params.linearDeflection = 0.1;
    params.angularDeflection = 0.5;
    params.computeNormals = true;
    params.computeUVs = true;
    return ExportMeshImpl(inputPath, outputPath, primPath, params);
}

// Quality-aware ABI. linearDeflection is the max chord distance (or, when
// relativeDeflection != 0, a fraction of the shape's bounding-box diagonal);
// angularDeflection is the max angle (radians) between adjacent facet normals,
// which is what smooths high-curvature silhouettes. Smaller values => more
// triangles => smoother arcs at close zoom.
HDOCCT_API
int UsdSolid_ExportMeshEx(const char* inputPath, const char* outputPath,
                          const char* primPath,
                          double linearDeflection, double angularDeflection,
                          int relativeDeflection) {
    UsdSolidTessellationParams params;
    params.linearDeflection = linearDeflection;
    params.angularDeflection = angularDeflection;
    params.relativeDeflection = (relativeDeflection != 0);
    params.computeNormals = true;
    params.computeUVs = true;
    return ExportMeshImpl(inputPath, outputPath, primPath, params);
}

}  // extern "C"

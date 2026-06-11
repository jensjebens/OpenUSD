// hdOcct mesh export API — tessellates BrepArray and writes USD mesh.
// Single source of truth for CLI and programmatic mesh export.
#include "tessellator.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/vt/array.h"

#include <cstdio>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

int
HdOcctExportMesh(
    const std::string& inputPath,
    const std::string& outputPath,
    const std::string& primPath,
    const UsdSolidTessellationParams& params)
{
    auto inputStage = UsdStage::Open(inputPath);
    if (!inputStage) return -1;

    UsdPrim brepPrim = inputStage->GetPrimAtPath(SdfPath(primPath));
    if (!brepPrim) return -2;

    UsdSolidTessellator tessellator;
    auto results = tessellator.Tessellate(brepPrim, params);
    fprintf(stdout, "Tessellated %zu bodies\n", results.size());

    auto outStage = UsdStage::CreateNew(outputPath);
    outStage->SetMetadata(UsdGeomTokens->upAxis, VtValue(TfToken("Y")));
    auto world = UsdGeomXform::Define(outStage, SdfPath("/World"));
    outStage->SetDefaultPrim(world.GetPrim());

    static const GfVec3f kDefaultDisplayColor(0.85f, 0.87f, 0.9f);

    int meshCount = 0;
    int totalVerts = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        auto& result = results[i];
        if (result.points.empty()) continue;

        // Compact: remove unreferenced vertices, remap indices
        result.Compact();
        totalVerts += result.points.size();

        std::string meshPathStr = "/World/mesh_" + std::to_string(i);
        auto mesh = UsdGeomMesh::Define(outStage, SdfPath(meshPathStr));

        // Convert double → float points for USD mesh
        VtArray<GfVec3f> points3f(result.points.size());
        for (size_t j = 0; j < result.points.size(); ++j) {
            const auto& p = result.points[j];
            points3f[j] = GfVec3f(
                static_cast<float>(p[0]),
                static_cast<float>(p[1]),
                static_cast<float>(p[2]));
        }

        mesh.GetPointsAttr().Set(points3f);
        mesh.GetFaceVertexCountsAttr().Set(result.faceVertexCounts);
        mesh.GetFaceVertexIndicesAttr().Set(result.faceVertexIndices);
        mesh.GetSubdivisionSchemeAttr().Set(UsdGeomTokens->none);

        if (!result.normals.empty()) {
            mesh.GetNormalsAttr().Set(result.normals);
            mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
        }

        VtArray<GfVec3f> colors(1, kDefaultDisplayColor);
        mesh.GetDisplayColorAttr().Set(colors);
        meshCount++;
    }

    outStage->Save();
    fprintf(stdout, "Wrote %d meshes (%d total verts) to %s\n",
            meshCount, totalVerts, outputPath.c_str());
    return meshCount;
}

PXR_NAMESPACE_CLOSE_SCOPE

// Legacy C-callable wrapper for ctypes/Python consumers.
extern "C" int UsdSolid_ExportMesh(
    const char* inputPath, const char* outputPath, const char* primPath)
{
    PXR_NAMESPACE_USING_DIRECTIVE
    UsdSolidTessellationParams params;
    params.linearDeflection = 0.1;
    params.angularDeflection = 0.5;
    params.computeNormals = true;
    params.computeUVs = true;
    return HdOcctExportMesh(inputPath, outputPath, primPath, params);
}

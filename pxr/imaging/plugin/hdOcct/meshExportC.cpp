// C-callable mesh export for use via ctypes/Python
#include "tessellator.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/vt/array.h"
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

extern "C" {

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

        VtArray<GfVec3f> points3f;
        points3f.reserve(result.points.size());
        for (const auto& p : result.points) {
            points3f.push_back(GfVec3f((float)p[0], (float)p[1], (float)p[2]));
        }
        mesh.GetPointsAttr().Set(points3f);
        mesh.GetFaceVertexCountsAttr().Set(result.faceVertexCounts);
        mesh.GetFaceVertexIndicesAttr().Set(result.faceVertexIndices);

        if (!result.normals.empty()) {
            mesh.GetNormalsAttr().Set(result.normals);
            mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
        }

        VtArray<GfVec3f> colors(1, GfVec3f(0.85f, 0.87f, 0.9f));
        mesh.GetDisplayColorAttr().Set(colors);
        meshCount++;
    }

    outStage->Save();
    return meshCount;
}

} // extern "C"

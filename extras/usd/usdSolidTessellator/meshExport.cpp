// Standalone mesh export: tessellate BrepArray and write Mesh USD
#include "tessellator.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/base/vt/array.h"

#include <iostream>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.usd> <output.usd> [primPath]"
                  << std::endl;
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = argv[2];
    std::string primPath = (argc > 3) ? argv[3] : "/World/Brep0";

    // Open input stage
    auto inputStage = UsdStage::Open(inputPath);
    if (!inputStage) {
        std::cerr << "Failed to open: " << inputPath << std::endl;
        return 1;
    }

    UsdPrim brepPrim = inputStage->GetPrimAtPath(SdfPath(primPath));
    if (!brepPrim) {
        std::cerr << "Prim not found: " << primPath << std::endl;
        return 1;
    }

    // Tessellate
    UsdSolidTessellator tessellator;
    UsdSolidTessellationParams params;
    params.linearDeflection = 0.1;
    params.angularDeflection = 0.5;
    params.computeNormals = true;
    params.computeUVs = true;

    auto results = tessellator.Tessellate(brepPrim, params);
    std::cout << "Tessellated " << results.size() << " bodies" << std::endl;

    // Create output stage
    auto outStage = UsdStage::CreateNew(outputPath);
    outStage->SetMetadata(UsdGeomTokens->upAxis, VtValue(TfToken("Y")));
    
    auto world = UsdGeomXform::Define(outStage, SdfPath("/World"));
    outStage->SetDefaultPrim(world.GetPrim());

    int meshCount = 0;
    int totalVerts = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        if (result.points.empty()) continue;

        std::string meshPath = "/World/mesh_" + std::to_string(i);
        auto mesh = UsdGeomMesh::Define(outStage, SdfPath(meshPath));

        // Points (GfVec3d -> GfVec3f)
        VtArray<GfVec3f> points3f;
        points3f.reserve(result.points.size());
        for (const auto& p : result.points) {
            points3f.push_back(GfVec3f((float)p[0], (float)p[1], (float)p[2]));
        }
        mesh.GetPointsAttr().Set(points3f);

        // Face vertex counts and indices
        mesh.GetFaceVertexCountsAttr().Set(result.faceVertexCounts);
        mesh.GetFaceVertexIndicesAttr().Set(result.faceVertexIndices);

        // Normals
        if (!result.normals.empty()) {
            mesh.GetNormalsAttr().Set(result.normals);
            mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
        }

        // Display color
        VtArray<GfVec3f> colors(1, GfVec3f(0.85f, 0.87f, 0.9f));
        mesh.GetDisplayColorAttr().Set(colors);

        meshCount++;
        totalVerts += result.points.size();
    }

    outStage->Save();
    std::cout << "Wrote " << meshCount << " meshes (" << totalVerts
              << " total verts) to " << outputPath << std::endl;
    return 0;
}

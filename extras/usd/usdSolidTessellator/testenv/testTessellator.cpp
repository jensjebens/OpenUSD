// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Test: tessellate a simple cube BrepArray and verify mesh output.

#include "lib/tessellator.h"
#include "lib/meshExporter.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/base/tf/diagnostic.h"

#include <cassert>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char** argv)
{
    std::cout << "=== UsdSolidTessellator Test ===" << std::endl;

    // Open test stage with a BrepArray cube
    std::string testFile = "testCube.usda";
    if (argc > 1) testFile = argv[1];

    UsdStageRefPtr stage = UsdStage::Open(testFile);
    if (!stage) {
        std::cerr << "ERROR: Could not open stage: " << testFile << std::endl;
        return 1;
    }

    // Find the BrepArray prim
    UsdPrim brepPrim = stage->GetPrimAtPath(SdfPath("/World/Cube"));
    if (!brepPrim.IsValid()) {
        std::cerr << "ERROR: No prim at /World/Cube" << std::endl;
        return 1;
    }

    // Tessellate
    UsdSolidTessellator tessellator;
    UsdSolidTessellationParams params;
    params.linearDeflection = 0.01;
    params.angularDeflection = 0.5;
    params.computeNormals = true;
    params.computeUVs = true;

    auto results = tessellator.Tessellate(brepPrim, params);

    if (results.empty()) {
        std::cerr << "ERROR: No tessellation results" << std::endl;
        return 1;
    }

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        if (!r.success) {
            std::cerr << "ERROR: Brep " << i << " failed: "
                      << r.errorMessage << std::endl;
            return 1;
        }

        std::cout << "Brep " << i << ": "
                  << r.points.size() << " vertices, "
                  << r.faceVertexCounts.size() << " triangles"
                  << std::endl;

        // A cube should produce at minimum 12 triangles (2 per face * 6 faces)
        assert(r.faceVertexCounts.size() >= 12);
        assert(r.points.size() >= 8);

        // All face vertex counts should be 3 (triangles)
        for (const auto& fc : r.faceVertexCounts) {
            assert(fc == 3);
        }
    }

    // Test export to stage
    UsdStageRefPtr outStage = UsdStage::CreateInMemory();
    UsdSolidMeshExporter exporter;
    auto meshPaths = exporter.ExportAll(
        outStage, SdfPath("/Meshes"), results);

    assert(!meshPaths.empty());

    for (const auto& path : meshPaths) {
        UsdGeomMesh mesh(outStage->GetPrimAtPath(path));
        assert(mesh);

        VtArray<GfVec3f> points;
        mesh.GetPointsAttr().Get(&points);
        assert(!points.empty());

        std::cout << "Exported mesh at " << path.GetText()
                  << ": " << points.size() << " points" << std::endl;
    }

    // Save output for inspection
    std::string outFile = "testCube_tessellated.usda";
    outStage->GetRootLayer()->Export(outFile);
    std::cout << "Wrote: " << outFile << std::endl;

    std::cout << "=== ALL TESTS PASSED ===" << std::endl;
    return 0;
}

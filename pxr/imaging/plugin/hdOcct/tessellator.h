// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Tessellator: High-level interface that takes a UsdSolid BrepArray prim
// and produces tessellated mesh data suitable for creating UsdGeomMesh prims.

#ifndef USD_SOLID_TESSELLATOR_TESSELLATOR_H
#define USD_SOLID_TESSELLATOR_TESSELLATOR_H

#include "api.h"

#include "pxr/pxr.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/path.h"

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \struct UsdSolidTessellationParams
///
/// Parameters controlling tessellation quality and behavior.
///
struct USDSOLIDTESSELLATOR_API UsdSolidTessellationParams {
    /// Linear deflection: max distance between the tessellation chord
    /// and the true surface. Smaller = more triangles, higher fidelity.
    double linearDeflection = 0.1;

    /// Angular deflection (radians): max angle between adjacent triangle
    /// normals. Controls tessellation of high-curvature regions.
    double angularDeflection = 0.5;

    /// If true, compute per-vertex normals from the parametric surface.
    /// If false, use flat face normals.
    bool computeNormals = true;

    /// If true, compute UV texture coordinates from the parametric domain.
    bool computeUVs = true;

    /// If true, produce a single merged mesh per BrepArray.
    /// If false, produce one mesh per Brep in the array.
    bool mergeBreps = false;

    /// Relative deflection mode: linearDeflection is interpreted as a
    /// fraction of the bounding box diagonal rather than an absolute distance.
    bool relativeDeflection = false;
};

/// \struct UsdSolidTessellationResult
///
/// Tessellated mesh data for a single Brep or merged BrepArray.
///
struct USDSOLIDTESSELLATOR_API UsdSolidTessellationResult {
    /// Triangle mesh vertices (double precision from OCCT).
    VtArray<GfVec3d> points;

    /// Face vertex counts (all 3 for triangles).
    VtArray<int> faceVertexCounts;

    /// Face vertex indices into the points array.
    VtArray<int> faceVertexIndices;

    /// Per-vertex normals (if computeNormals was set).
    VtArray<GfVec3f> normals;

    /// Per-vertex UV coordinates (if computeUVs was set).
    VtArray<GfVec2f> uvs;

    /// Per-face source Brep index (useful for material assignment).
    VtArray<int> faceBrepIndices;

    /// Per-face source face index within the Brep (for GeomSubset creation).
    VtArray<int> faceSolidFaceIndices;

    /// Whether tessellation succeeded.
    bool success = false;

    /// Error message if tessellation failed.
    std::string errorMessage;
};

/// \class UsdSolidTessellator
///
/// Tessellates UsdSolid BrepArray prims into triangle meshes using
/// OpenCascade's BRepMesh_IncrementalMesh algorithm.
///
/// Usage:
/// \code
///   UsdSolidTessellator tess;
///   UsdSolidTessellationParams params;
///   params.linearDeflection = 0.01;  // fine tessellation
///
///   auto results = tess.Tessellate(brepArrayPrim, params);
///   for (auto& result : results) {
///       // Create UsdGeomMesh from result.points, result.faceVertexIndices, etc.
///   }
/// \endcode
///
class USDSOLIDTESSELLATOR_API UsdSolidTessellator {
public:
    UsdSolidTessellator();
    ~UsdSolidTessellator();

    /// Tessellate all Breps in a BrepArray prim.
    /// Returns one result per Brep (or one merged result if params.mergeBreps).
    std::vector<UsdSolidTessellationResult> Tessellate(
        const UsdPrim& brepArrayPrim,
        const UsdSolidTessellationParams& params = {}) const;

    /// Tessellate a single Brep at \p brepIndex from the BrepArray.
    UsdSolidTessellationResult TessellateSingle(
        const UsdPrim& brepArrayPrim,
        size_t brepIndex,
        const UsdSolidTessellationParams& params = {}) const;

    /// Convenience: tessellate and write mesh prims under \p destPath.
    /// Creates UsdGeomMesh prims as children of destPath.
    /// Returns the paths of created mesh prims.
    std::vector<SdfPath> TessellateToStage(
        const UsdPrim& brepArrayPrim,
        const SdfPath& destPath,
        const UsdSolidTessellationParams& params = {}) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_SOLID_TESSELLATOR_TESSELLATOR_H

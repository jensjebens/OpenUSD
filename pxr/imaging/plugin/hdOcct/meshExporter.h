// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// MeshExporter: Utility to write tessellated results as UsdGeomMesh prims.

#ifndef USD_SOLID_TESSELLATOR_MESH_EXPORTER_H
#define USD_SOLID_TESSELLATOR_MESH_EXPORTER_H

#include "api.h"
#include "tessellator.h"

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/path.h"

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdSolidMeshExporter
///
/// Writes tessellation results to a UsdStage as UsdGeomMesh prims.
/// Handles normal primvars, UV primvars, extent computation, and
/// GeomSubset creation for per-face material assignment.
///
class USDSOLIDTESSELLATOR_API UsdSolidMeshExporter {
public:
    UsdSolidMeshExporter();
    ~UsdSolidMeshExporter();

    /// Export a single tessellation result as a UsdGeomMesh at \p meshPath.
    /// Returns the created mesh path, or an empty path on failure.
    SdfPath Export(
        const UsdStageRefPtr& stage,
        const SdfPath& meshPath,
        const UsdSolidTessellationResult& result) const;

    /// Export multiple tessellation results under \p parentPath.
    /// Creates meshes named "mesh_0", "mesh_1", etc.
    std::vector<SdfPath> ExportAll(
        const UsdStageRefPtr& stage,
        const SdfPath& parentPath,
        const std::vector<UsdSolidTessellationResult>& results) const;

    /// If true, create GeomSubsets for per-face material binding based on
    /// the faceSolidFaceIndices in the tessellation result.
    bool createGeomSubsets = true;

    /// If true, compute and author the extent attribute.
    bool computeExtent = true;

    /// Subdivision scheme for the authored mesh. Default "none" for triangles.
    TfToken subdivisionScheme = TfToken("none");
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_SOLID_TESSELLATOR_MESH_EXPORTER_H

// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Takes a UsdSolid BrepArray prim and produces tessellated mesh data, using
// SMLib as the geometry kernel.
//
// The interface deliberately mirrors hdOcct's UsdSolidTessellator so the
// Hydra-side pieces -- the UsdImaging adapter and the hdGp procedural -- are
// identical between the two plugins and only the kernel differs.

#ifndef PXR_IMAGING_PLUGIN_HD_SMLIB_TESSELLATOR_H
#define PXR_IMAGING_PLUGIN_HD_SMLIB_TESSELLATOR_H

#include "api.h"

#include "pxr/pxr.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/usd/prim.h"

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Parameters controlling tessellation quality.
///
/// SMLib is driven by chord height and angle rather than OCCT's linear and
/// angular deflection. The names here follow SMLib; the defaults match what
/// SmApiTessellate is normally called with.
struct HDSMLIB_API UsdSolidSmlibTessellationParams {
    /// Maximum distance between a facet and the true surface.
    double chordHeightTolerance = 0.001;

    /// Maximum angle, in degrees, between adjacent facet normals. Zero
    /// disables the angular criterion. Note SMLib takes degrees where OCCT
    /// takes radians.
    double angleToleranceDegrees = 0.0;

    /// Maximum facet edge length. Zero disables the constraint.
    double maxEdgeLength = 0.0;

    /// Maximum facet aspect ratio. Zero disables the constraint.
    double maxAspectRatio = 0.0;

    /// Run SMLib's healer while converting from the schema. Disable to see
    /// what the source data actually contains.
    bool healerEnabled = true;
};

/// Tessellated mesh data for one Brep.
struct HDSMLIB_API UsdSolidSmlibTessellationResult {
    VtArray<GfVec3f> points;
    VtIntArray faceVertexCounts;
    VtIntArray faceVertexIndices;
    VtArray<GfVec3f> normals;
    VtIntArray normalsIndices;
    TfToken normalsInterpolation;
    TfToken subdivisionScheme;

    /// Bounding box corners, as returned by the kernel.
    VtArray<GfVec3f> extent;

    /// Per-face index sets, one entry per GeomSubset the source Brep carries.
    /// SMLib populates this from the Brep's face groupings; hdOcct has no
    /// equivalent.
    VtArray<VtIntArray> geomSubsetIndices;

    /// Index of this Brep within the source BrepArray.
    int brepIndex = -1;

    bool success = false;
    std::string errorMessage;
};

/// Tessellates UsdSolid BrepArray prims into triangle meshes with SMLib.
///
/// \code
///   UsdSolidSmlibTessellator tess;
///   for (const auto &r : tess.Tessellate(brepArrayPrim)) {
///       // r.points, r.faceVertexIndices, ...
///   }
/// \endcode
class HDSMLIB_API UsdSolidSmlibTessellator {
public:
    UsdSolidSmlibTessellator();
    ~UsdSolidSmlibTessellator();

    UsdSolidSmlibTessellator(const UsdSolidSmlibTessellator &) = delete;
    UsdSolidSmlibTessellator &operator=(const UsdSolidSmlibTessellator &) = delete;

    /// Tessellate every Brep in \p brepArrayPrim, one result per Brep, in the
    /// order the BrepArray stores them. A Brep that fails to tessellate still
    /// gets a result, with success false and errorMessage set, so callers can
    /// report per-body failures rather than losing the whole array.
    std::vector<UsdSolidSmlibTessellationResult> Tessellate(
        const UsdPrim &brepArrayPrim,
        const UsdSolidSmlibTessellationParams &params = {}) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_PLUGIN_HD_SMLIB_TESSELLATOR_H

// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// BrepBuilder: Converts UsdSolid BrepArray schema data into an OpenCascade
// TopoDS_Shape by reconstructing the Radial Edge topology.

#ifndef USD_SOLID_TESSELLATOR_BREP_BUILDER_H
#define USD_SOLID_TESSELLATOR_BREP_BUILDER_H

#include "api.h"

#include "pxr/pxr.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/usd/usd/prim.h"

#include <TopoDS_Shape.hxx>
#include <vector>
#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdSolidBrepBuilder
///
/// Reconstructs an OpenCascade TopoDS_Shape from the flat-packed BrepArray
/// attributes defined in USD Proposal #109 (UsdSolid).
///
/// The builder reads the following attribute families:
///   - brep:* (counts, extents, tolerances)
///   - region:* (shell counts, types)
///   - shell:* (faceuse counts, wire edges)
///   - face:* (loop counts, surface types, trim types, ranges)
///   - loop:* (edgeuse counts, vertex indices)
///   - edgeuse:* (edge indices, orientations, radial connectivity)
///   - edge:* (curve types, ranges, vertex indices)
///   - wireEdge:* (curve types, ranges, vertex indices)
///   - vertex:* (point types)
///   - BrepPointAPI (vertex positions, shell points)
///   - BrepCurve3dNurbAPI (3D NURBS curves for edges/wireEdges)
///   - BrepCurveUvNurbAPI (2D trim curves for edgeuses)
///   - BrepSurfaceNurbAPI (NURBS surfaces for faces)
///
class USDSOLIDTESSELLATOR_API UsdSolidBrepBuilder {
public:
    UsdSolidBrepBuilder();
    ~UsdSolidBrepBuilder();

    /// Build OCCT shape(s) from a UsdSolid BrepArray prim.
    /// Returns one TopoDS_Shape per Brep in the array.
    std::vector<TopoDS_Shape> Build(const UsdPrim& brepArrayPrim) const;

    /// Build a single Brep at \p brepIndex from the BrepArray.
    std::optional<TopoDS_Shape> BuildSingleBrep(
        const UsdPrim& brepArrayPrim,
        size_t brepIndex) const;

private:
    // --- Internal data extraction ---

    struct _BrepData {
        // Counts
        VtArray<unsigned int> regionCount;
        double intersectTol3d = 1e-6;

        // Region
        VtArray<unsigned int> regionShellCount;
        VtArray<TfToken> regionType;

        // Shell
        VtArray<unsigned int> shellFaceuseCount;
        VtArray<unsigned int> shellWireEdgeCount;

        // Face
        VtArray<unsigned int> faceLoopCount;
        VtArray<TfToken> faceSurfaceType;
        VtArray<TfToken> faceTrimType;
        VtArray<GfVec2d> faceRange; // pairs of UVmin, UVmax

        // Loop
        VtArray<unsigned int> loopEdgeuseCount;
        VtArray<unsigned int> loopVertexIndex;

        // Edgeuse
        VtArray<unsigned int> edgeuseEdgeIndex;
        VtArray<TfToken> edgeuseOrientationType;
        VtArray<TfToken> faceuseOrientationType;
        VtArray<unsigned int> edgeuseNextRadialEUIndex;

        // Edge
        VtArray<TfToken> edgeCurveType;
        VtArray<double> edgeRange; // pairs {paramMin, paramMax}
        VtArray<GfVec2i> edgeVertexIndices; // {start, end}

        // WireEdge
        VtArray<TfToken> wireEdgeCurveType;
        VtArray<double> wireEdgeRange;
        VtArray<GfVec2i> wireEdgeVertexIndices;

        // Vertex points (from BrepPointAPI:vertexPoint)
        VtArray<GfVec3d> vertexPositions;

        // 3D NURBS curves for edges (from BrepCurve3dNurbAPI:edge3dNurb)
        VtArray<GfVec3d> edgeCurveControlVertices;
        VtArray<unsigned int> edgeCurveVertexCount;
        VtArray<unsigned int> edgeCurveOrder;
        VtArray<double> edgeCurveKnots;
        VtArray<double> edgeCurveWeights;

        // 3D NURBS curves for wireEdges (from BrepCurve3dNurbAPI:wireEdge3dNurb)
        VtArray<GfVec3d> wireEdgeCurveControlVertices;
        VtArray<unsigned int> wireEdgeCurveVertexCount;
        VtArray<unsigned int> wireEdgeCurveOrder;
        VtArray<double> wireEdgeCurveKnots;
        VtArray<double> wireEdgeCurveWeights;

        // UV trim curves (from BrepCurveUvNurbAPI)
        VtArray<GfVec2d> trimCurveControlVertices;
        VtArray<unsigned int> trimCurveVertexCount;
        VtArray<unsigned int> trimCurveOrder;
        VtArray<double> trimCurveKnots;
        VtArray<double> trimCurveWeights;

        // NURBS surfaces (from BrepSurfaceNurbAPI)
        VtArray<GfVec3d> surfaceControlVertices;
        VtArray<unsigned int> surfaceUVertexCount;
        VtArray<unsigned int> surfaceVVertexCount;
        VtArray<unsigned int> surfaceUOrder;
        VtArray<unsigned int> surfaceVOrder;
        VtArray<double> surfaceUKnots;
        VtArray<double> surfaceVKnots;
        VtArray<double> surfaceWeights;
    };

    bool _ReadBrepData(const UsdPrim& prim, _BrepData* data) const;

    TopoDS_Shape _BuildSingleBrep(
        const _BrepData& data,
        size_t brepIndex) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_SOLID_TESSELLATOR_BREP_BUILDER_H

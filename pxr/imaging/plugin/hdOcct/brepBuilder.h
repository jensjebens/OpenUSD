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

    /// Authored face indices of the faces present in the shape(s) returned
    /// by the most recent Build/BuildSingleBrep call, one vector per built
    /// Brep, in compound face order. The build ladder can drop faces, so
    /// compound position != authored face index; per-face attribution
    /// (colors, geom subsets) must map through this. An empty per-Brep
    /// vector means the mapping is unknown (the HDOCCT_SEW path rewrites
    /// topology and invalidates it).
    const std::vector<std::vector<int>>& GetBuiltFaceAuthoredIndices() const
        { return _builtFaceAuthored; }

private:
    // --- Internal data extraction ---

    struct _BrepData {
        // Counts
        VtArray<unsigned int> regionCount;
        // Per-Brep 3D intersection tolerance (brep:intersectTol3d authors one
        // entry per Brep in the array). Resolved per Brep in _BuildSingleBrep
        // with a last-entry clamp; empty means "use the 1e-6 reader fallback"
        // (debt register row 17). Previously collapsed to entry [0] for every
        // Brep.
        VtArray<double> intersectTol3d;

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
        VtArray<unsigned int> faceuseFaceIndex;
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

        // Analytic surfaces (BrepSurfaceCylinderAPI; more types to follow).
        // Each array is packed in order used by faces of that surface type.
        VtArray<GfVec3d> surfaceCylinderOrigin;
        VtArray<GfVec3d> surfaceCylinderAxis;
        VtArray<GfVec3d> surfaceCylinderRefDir;
        VtArray<double>  surfaceCylinderRadius;
        VtArray<GfVec3d> surfacePlaneOrigin;
        VtArray<GfVec3d> surfacePlaneAxis;
        VtArray<GfVec3d> surfacePlaneRefDir;
        VtArray<GfVec3d> surfaceConeOrigin;
        VtArray<GfVec3d> surfaceConeAxis;
        VtArray<GfVec3d> surfaceConeRefDir;
        VtArray<double>  surfaceConeRadius;
        VtArray<double>  surfaceConeSemiAngle;
        VtArray<GfVec3d> surfaceSphereCenter;
        VtArray<GfVec3d> surfaceSphereAxis;
        VtArray<GfVec3d> surfaceSphereRefDir;
        VtArray<double>  surfaceSphereRadius;
        VtArray<GfVec3d> surfaceTorusOrigin;
        VtArray<GfVec3d> surfaceTorusAxis;
        VtArray<GfVec3d> surfaceTorusRefDir;
        VtArray<double>  surfaceTorusMajorRadius;
        VtArray<double>  surfaceTorusMinorRadius;

        // Analytic 3D curves for edges (line/circle/ellipse), packed per type.
        VtArray<GfVec3d> edgeLineOrigin;
        VtArray<GfVec3d> edgeLineDirection;
        VtArray<GfVec3d> edgeCircleCenter;
        VtArray<GfVec3d> edgeCircleAxis;
        VtArray<GfVec3d> edgeCircleRefDir;
        VtArray<double>  edgeCircleRadius;
        VtArray<GfVec3d> edgeEllipseCenter;
        VtArray<GfVec3d> edgeEllipseAxis;
        VtArray<GfVec3d> edgeEllipseRefDir;
        VtArray<double>  edgeEllipseXRadius;
        VtArray<double>  edgeEllipseYRadius;
    };

    bool _ReadBrepData(const UsdPrim& prim, _BrepData* data) const;

    TopoDS_Shape _BuildSingleBrep(
        const _BrepData& data,
        size_t brepIndex,
        std::vector<int>* outFaceAuthoredIndices = nullptr) const;

    mutable std::vector<std::vector<int>> _builtFaceAuthored;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_SOLID_TESSELLATOR_BREP_BUILDER_H

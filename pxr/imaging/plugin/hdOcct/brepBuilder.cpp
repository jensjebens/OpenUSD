// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// BrepBuilder implementation: reconstructs OCCT TopoDS_Shape from
// UsdSolid BrepArray flat-packed attributes.

#include "brepBuilder.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/usd/attribute.h"

// OpenCascade includes
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <BRepLib.hxx>
#include <GeomProjLib.hxx>
#include <Geom2d_Curve.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_ElementarySurface.hxx>
#include <Standard_Failure.hxx>
#include <TopLoc_Location.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <gp_Pnt.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Wire.hxx>
#include <ShapeFix_Face.hxx>
#include <BRepLib.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
// Used for rational B-spline surface weights; OCCT 7.x provided this
// transitively, OCCT 8.0 does not — include it explicitly.
#include <TColStd_Array2OfReal.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopExp_Explorer.hxx>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (BrepSurfaceNurbAPI)
    (BrepCurve3dNurbAPI)
    (solidRegion)
    (voidRegion)
    (same)
    (opposite)
    (rectangular)
    (general)
);

// --------------------------------------------------------------------------
// Helper: extract knot vector from flat packed NURBS data
// --------------------------------------------------------------------------
namespace {

/// Decompose a flat knot array into multiplicity form for OCCT.
/// USD stores knots with explicit repeats; OCCT wants (knot, mult) pairs.
void _DecomposeKnots(
    const double* knots,
    int numKnots,
    std::vector<double>& outKnots,
    std::vector<int>& outMults)
{
    outKnots.clear();
    outMults.clear();
    if (numKnots == 0) return;

    outKnots.push_back(knots[0]);
    outMults.push_back(1);

    for (int i = 1; i < numKnots; ++i) {
        if (std::abs(knots[i] - outKnots.back()) < 1e-15) {
            outMults.back()++;
        } else {
            outKnots.push_back(knots[i]);
            outMults.push_back(1);
        }
    }
}

/// Build a Geom_BSplineCurve from packed USD NURBS curve data.
Handle(Geom_BSplineCurve) _MakeBSplineCurve(
    const GfVec3d* controlPts,
    int numPts,
    int order,
    const double* knots,
    int numKnots,
    const double* weights,
    int numWeights)
{
    if (numPts < 2 || order < 2) return nullptr;

    // Control points
    TColgp_Array1OfPnt poles(1, numPts);
    for (int i = 0; i < numPts; ++i) {
        poles.SetValue(i + 1, gp_Pnt(controlPts[i][0],
                                       controlPts[i][1],
                                       controlPts[i][2]));
    }

    // Weights
    TColStd_Array1OfReal w(1, numPts);
    bool hasWeights = (weights != nullptr && numWeights == numPts);
    for (int i = 0; i < numPts; ++i) {
        w.SetValue(i + 1, hasWeights ? weights[i] : 1.0);
    }

    // Knots -> (knot, multiplicity)
    std::vector<double> knotVals;
    std::vector<int> mults;
    _DecomposeKnots(knots, numKnots, knotVals, mults);

    if (knotVals.empty()) return nullptr;

    TColStd_Array1OfReal knotsArr(1, (int)knotVals.size());
    TColStd_Array1OfInteger multsArr(1, (int)mults.size());
    for (size_t i = 0; i < knotVals.size(); ++i) {
        knotsArr.SetValue((int)i + 1, knotVals[i]);
        multsArr.SetValue((int)i + 1, mults[i]);
    }

    int degree = order - 1;

    try {
        if (hasWeights) {
            return new Geom_BSplineCurve(poles, w, knotsArr, multsArr, degree);
        } else {
            return new Geom_BSplineCurve(poles, knotsArr, multsArr, degree);
        }
    } catch (...) {
        TF_WARN("Failed to construct BSplineCurve: degree=%d, poles=%d, "
                 "knots=%d", degree, numPts, (int)knotVals.size());
        return nullptr;
    }
}

/// Build a Geom_BSplineSurface from packed USD NURBS surface data.
Handle(Geom_BSplineSurface) _MakeBSplineSurface(
    const GfVec3d* controlPts,
    int uCount,
    int vCount,
    int uOrder,
    int vOrder,
    const double* uKnots,
    int numUKnots,
    const double* vKnots,
    int numVKnots,
    const double* weights,
    int numWeights)
{
    if (uCount < 2 || vCount < 2) return nullptr;

    int uDegree = uOrder - 1;
    int vDegree = vOrder - 1;

    // Control points (row-major U=rows, V=cols)
    TColgp_Array2OfPnt poles(1, uCount, 1, vCount);
    TColStd_Array2OfReal w(1, uCount, 1, vCount);
    bool hasWeights = (weights != nullptr && numWeights == uCount * vCount);

    for (int u = 0; u < uCount; ++u) {
        for (int v = 0; v < vCount; ++v) {
            int idx = u * vCount + v;
            poles.SetValue(u + 1, v + 1,
                          gp_Pnt(controlPts[idx][0],
                                 controlPts[idx][1],
                                 controlPts[idx][2]));
            w.SetValue(u + 1, v + 1, hasWeights ? weights[idx] : 1.0);
        }
    }

    // U knots
    std::vector<double> uKnotVals, vKnotVals;
    std::vector<int> uMults, vMults;
    _DecomposeKnots(uKnots, numUKnots, uKnotVals, uMults);
    _DecomposeKnots(vKnots, numVKnots, vKnotVals, vMults);

    if (uKnotVals.empty() || vKnotVals.empty()) return nullptr;

    TColStd_Array1OfReal uKnotsArr(1, (int)uKnotVals.size());
    TColStd_Array1OfInteger uMultsArr(1, (int)uMults.size());
    for (size_t i = 0; i < uKnotVals.size(); ++i) {
        uKnotsArr.SetValue((int)i + 1, uKnotVals[i]);
        uMultsArr.SetValue((int)i + 1, uMults[i]);
    }

    TColStd_Array1OfReal vKnotsArr(1, (int)vKnotVals.size());
    TColStd_Array1OfInteger vMultsArr(1, (int)vMults.size());
    for (size_t i = 0; i < vKnotVals.size(); ++i) {
        vKnotsArr.SetValue((int)i + 1, vKnotVals[i]);
        vMultsArr.SetValue((int)i + 1, vMults[i]);
    }

    try {
        if (hasWeights) {
            return new Geom_BSplineSurface(
                poles, w, uKnotsArr, vKnotsArr,
                uMultsArr, vMultsArr, uDegree, vDegree);
        } else {
            return new Geom_BSplineSurface(
                poles, uKnotsArr, vKnotsArr,
                uMultsArr, vMultsArr, uDegree, vDegree);
        }
    } catch (...) {
        TF_WARN("Failed to construct BSplineSurface: uDeg=%d vDeg=%d "
                 "uPoles=%d vPoles=%d", uDegree, vDegree, uCount, vCount);
        return nullptr;
    }
}

/// Build a 2D BSpline trim curve.
Handle(Geom2d_BSplineCurve) _MakeBSplineCurve2d(
    const GfVec2d* controlPts,
    int numPts,
    int order,
    const double* knots,
    int numKnots,
    const double* weights,
    int numWeights)
{
    if (numPts < 2 || order < 2) return nullptr;

    TColgp_Array1OfPnt2d poles(1, numPts);
    for (int i = 0; i < numPts; ++i) {
        poles.SetValue(i + 1, gp_Pnt2d(controlPts[i][0], controlPts[i][1]));
    }

    TColStd_Array1OfReal w(1, numPts);
    bool hasWeights = (weights != nullptr && numWeights == numPts);
    for (int i = 0; i < numPts; ++i) {
        w.SetValue(i + 1, hasWeights ? weights[i] : 1.0);
    }

    std::vector<double> knotVals;
    std::vector<int> mults;
    _DecomposeKnots(knots, numKnots, knotVals, mults);

    if (knotVals.empty()) return nullptr;

    TColStd_Array1OfReal knotsArr(1, (int)knotVals.size());
    TColStd_Array1OfInteger multsArr(1, (int)mults.size());
    for (size_t i = 0; i < knotVals.size(); ++i) {
        knotsArr.SetValue((int)i + 1, knotVals[i]);
        multsArr.SetValue((int)i + 1, mults[i]);
    }

    int degree = order - 1;
    try {
        if (hasWeights) {
            return new Geom2d_BSplineCurve(poles, w, knotsArr, multsArr, degree);
        } else {
            return new Geom2d_BSplineCurve(poles, knotsArr, multsArr, degree);
        }
    } catch (...) {
        return nullptr;
    }
}

} // anonymous namespace

// --------------------------------------------------------------------------
// UsdSolidBrepBuilder
// --------------------------------------------------------------------------

UsdSolidBrepBuilder::UsdSolidBrepBuilder() = default;
UsdSolidBrepBuilder::~UsdSolidBrepBuilder() = default;

bool
UsdSolidBrepBuilder::_ReadBrepData(const UsdPrim& prim, _BrepData* data) const
{
    if (!prim.IsValid()) {
        TF_WARN("Invalid prim for BrepArray read");
        return false;
    }

    auto _GetAttr = [&prim](const char* name, auto& out) -> bool {
        UsdAttribute attr = prim.GetAttribute(TfToken(name));
        if (!attr) return false;
        return attr.Get(&out);
    };

    // Brep-level
    VtArray<double> tolArray;
    if (_GetAttr("brep:intersectTol3d", tolArray) && !tolArray.empty()) {
        data->intersectTol3d = tolArray[0];
    }
    _GetAttr("brep:regionCount", data->regionCount);

    // Region
    _GetAttr("region:shellCount", data->regionShellCount);
    _GetAttr("region:type", data->regionType);

    // Shell
    _GetAttr("shell:faceuseCount", data->shellFaceuseCount);
    _GetAttr("shell:wireEdgeCount", data->shellWireEdgeCount);

    // Face
    _GetAttr("face:loopCount", data->faceLoopCount);
    _GetAttr("face:surfaceType", data->faceSurfaceType);
    _GetAttr("face:trimType", data->faceTrimType);
    _GetAttr("face:range", data->faceRange);

    // Loop
    _GetAttr("loop:edgeuseCount", data->loopEdgeuseCount);
    _GetAttr("loop:vertexIndex", data->loopVertexIndex);

    // Edgeuse
    _GetAttr("edgeuse:edgeIndex", data->edgeuseEdgeIndex);
    _GetAttr("edgeuse:orientationType", data->edgeuseOrientationType);
    _GetAttr("faceuse:orientationType", data->faceuseOrientationType);
    _GetAttr("faceuse:faceIndex", data->faceuseFaceIndex);
    _GetAttr("edgeuse:nextRadialEUIndex", data->edgeuseNextRadialEUIndex);

    // Edge
    _GetAttr("edge:curveType", data->edgeCurveType);
    _GetAttr("edge:range", data->edgeRange);
    _GetAttr("edge:vertexIndices", data->edgeVertexIndices);

    // WireEdge
    _GetAttr("wireEdge:curveType", data->wireEdgeCurveType);
    _GetAttr("wireEdge:range", data->wireEdgeRange);
    _GetAttr("wireEdge:vertexIndices", data->wireEdgeVertexIndices);

    // Vertex points (BrepPointAPI:vertexPoint)
    _GetAttr("brep:vertexPoint:point:position", data->vertexPositions);

    // Edge 3D NURBS (BrepCurve3dNurbAPI:edge3dNurb)
    _GetAttr("brep:edge3dNurb:curve3d:nurb:controlVertices",
             data->edgeCurveControlVertices);
    _GetAttr("brep:edge3dNurb:curve3d:nurb:vertexCount",
             data->edgeCurveVertexCount);
    _GetAttr("brep:edge3dNurb:curve3d:nurb:order",
             data->edgeCurveOrder);
    _GetAttr("brep:edge3dNurb:curve3d:nurb:knots",
             data->edgeCurveKnots);
    _GetAttr("brep:edge3dNurb:curve3d:nurb:weights",
             data->edgeCurveWeights);

    // WireEdge 3D NURBS (BrepCurve3dNurbAPI:wireEdge3dNurb)
    _GetAttr("brep:wireEdge3dNurb:curve3d:nurb:controlVertices",
             data->wireEdgeCurveControlVertices);
    _GetAttr("brep:wireEdge3dNurb:curve3d:nurb:vertexCount",
             data->wireEdgeCurveVertexCount);
    _GetAttr("brep:wireEdge3dNurb:curve3d:nurb:order",
             data->wireEdgeCurveOrder);
    _GetAttr("brep:wireEdge3dNurb:curve3d:nurb:knots",
             data->wireEdgeCurveKnots);
    _GetAttr("brep:wireEdge3dNurb:curve3d:nurb:weights",
             data->wireEdgeCurveWeights);

    // UV trim curves (BrepCurveUvNurbAPI)
    _GetAttr("brep:curveUv:nurb:controlVertices",
             data->trimCurveControlVertices);
    _GetAttr("brep:curveUv:nurb:vertexCount",
             data->trimCurveVertexCount);
    _GetAttr("brep:curveUv:nurb:order",
             data->trimCurveOrder);
    _GetAttr("brep:curveUv:nurb:knots",
             data->trimCurveKnots);
    _GetAttr("brep:curveUv:nurb:weights",
             data->trimCurveWeights);

    // NURBS surfaces (BrepSurfaceNurbAPI)
    _GetAttr("brep:surface:nurb:controlVertices",
             data->surfaceControlVertices);
    _GetAttr("brep:surface:nurb:uVertexCount",
             data->surfaceUVertexCount);
    _GetAttr("brep:surface:nurb:vVertexCount",
             data->surfaceVVertexCount);
    _GetAttr("brep:surface:nurb:uOrder",
             data->surfaceUOrder);
    _GetAttr("brep:surface:nurb:vOrder",
             data->surfaceVOrder);
    _GetAttr("brep:surface:nurb:uKnots",
             data->surfaceUKnots);
    _GetAttr("brep:surface:nurb:vKnots",
             data->surfaceVKnots);
    _GetAttr("brep:surface:nurb:weights",
             data->surfaceWeights);

    // Analytic cylinder surfaces (BrepSurfaceCylinderAPI)
    _GetAttr("brep:surface:cylinder:origin", data->surfaceCylinderOrigin);
    _GetAttr("brep:surface:cylinder:axis", data->surfaceCylinderAxis);
    _GetAttr("brep:surface:cylinder:refDirection", data->surfaceCylinderRefDir);
    _GetAttr("brep:surface:cylinder:radius", data->surfaceCylinderRadius);
    _GetAttr("brep:surface:plane:origin", data->surfacePlaneOrigin);
    _GetAttr("brep:surface:plane:axis", data->surfacePlaneAxis);
    _GetAttr("brep:surface:plane:refDirection", data->surfacePlaneRefDir);
    _GetAttr("brep:surface:cone:origin", data->surfaceConeOrigin);
    _GetAttr("brep:surface:cone:axis", data->surfaceConeAxis);
    _GetAttr("brep:surface:cone:refDirection", data->surfaceConeRefDir);
    _GetAttr("brep:surface:cone:radius", data->surfaceConeRadius);
    _GetAttr("brep:surface:cone:semiAngle", data->surfaceConeSemiAngle);
    _GetAttr("brep:surface:sphere:center", data->surfaceSphereCenter);
    _GetAttr("brep:surface:sphere:axis", data->surfaceSphereAxis);
    _GetAttr("brep:surface:sphere:refDirection", data->surfaceSphereRefDir);
    _GetAttr("brep:surface:sphere:radius", data->surfaceSphereRadius);
    _GetAttr("brep:surface:torus:origin", data->surfaceTorusOrigin);
    _GetAttr("brep:surface:torus:axis", data->surfaceTorusAxis);
    _GetAttr("brep:surface:torus:refDirection", data->surfaceTorusRefDir);
    _GetAttr("brep:surface:torus:majorRadius", data->surfaceTorusMajorRadius);
    _GetAttr("brep:surface:torus:minorRadius", data->surfaceTorusMinorRadius);

    // Analytic 3D curves for edges (line/circle/ellipse)
    _GetAttr("brep:edge3dLine:curve3d:line:origin", data->edgeLineOrigin);
    _GetAttr("brep:edge3dLine:curve3d:line:direction", data->edgeLineDirection);
    _GetAttr("brep:edge3dCircle:curve3d:circle:center", data->edgeCircleCenter);
    _GetAttr("brep:edge3dCircle:curve3d:circle:axis", data->edgeCircleAxis);
    _GetAttr("brep:edge3dCircle:curve3d:circle:refDirection", data->edgeCircleRefDir);
    _GetAttr("brep:edge3dCircle:curve3d:circle:radius", data->edgeCircleRadius);
    _GetAttr("brep:edge3dEllipse:curve3d:ellipse:center", data->edgeEllipseCenter);
    _GetAttr("brep:edge3dEllipse:curve3d:ellipse:axis", data->edgeEllipseAxis);
    _GetAttr("brep:edge3dEllipse:curve3d:ellipse:refDirection", data->edgeEllipseRefDir);
    _GetAttr("brep:edge3dEllipse:curve3d:ellipse:xRadius", data->edgeEllipseXRadius);
    _GetAttr("brep:edge3dEllipse:curve3d:ellipse:yRadius", data->edgeEllipseYRadius);

    return !data->regionCount.empty();
}

TopoDS_Shape
UsdSolidBrepBuilder::_BuildSingleBrep(
    const _BrepData& data,
    size_t brepIndex) const
{
    BRep_Builder builder;

    // Determine offset indices for this Brep
    // Regions for this Brep start at sum of regionCount[0..brepIndex-1]
    size_t regionStart = 0;
    for (size_t i = 0; i < brepIndex && i < data.regionCount.size(); ++i) {
        regionStart += data.regionCount[i];
    }
    size_t numRegions = (brepIndex < data.regionCount.size())
                        ? data.regionCount[brepIndex] : 0;

    if (numRegions == 0) {
        TF_WARN("Brep %zu has 0 regions", brepIndex);
        return TopoDS_Shape();
    }

    // Compute shell start for this Brep's regions
    size_t shellStart = 0;
    for (size_t i = 0; i < regionStart && i < data.regionShellCount.size(); ++i) {
        shellStart += data.regionShellCount[i];
    }

    // Compute total shells for this Brep
    size_t totalShells = 0;
    for (size_t i = regionStart;
         i < regionStart + numRegions && i < data.regionShellCount.size(); ++i) {
        totalShells += data.regionShellCount[i];
    }

    // Compute faceuse start for this Brep's shells
    size_t faceuseStart = 0;
    for (size_t i = 0; i < shellStart && i < data.shellFaceuseCount.size(); ++i) {
        faceuseStart += data.shellFaceuseCount[i];
    }

    // Total faceuses for this Brep
    size_t totalFaceuses = 0;
    for (size_t i = shellStart;
         i < shellStart + totalShells && i < data.shellFaceuseCount.size(); ++i) {
        totalFaceuses += data.shellFaceuseCount[i];
    }

    // Number of faces = totalFaceuses / 2 (each face has two faceuses)
    size_t numFaces = totalFaceuses / 2;

    // Compute face start index: count faces in preceding bodies
    size_t faceStart = 0;
    {
        size_t prevShellStart = 0;
        for (size_t r = 0; r < regionStart && r < data.regionShellCount.size(); ++r) {
            prevShellStart += data.regionShellCount[r];
        }
        size_t prevFaceuses = 0;
        for (size_t s = 0; s < prevShellStart && s < data.shellFaceuseCount.size(); ++s) {
            prevFaceuses += data.shellFaceuseCount[s];
        }
        faceStart = prevFaceuses / 2;
    }

    // Build OCCT surfaces for each face
    std::vector<Handle(Geom_Surface)> surfaces;
    surfaces.reserve(numFaces);

    // Surface arrays are packed per surface TYPE (NURBS faces in the nurb
    // arrays, cylinder faces in the cylinder arrays, ...). Walk faces in order,
    // dispatch on face:surfaceType, and advance the matching per-type offsets.
    // Faces before faceStart (earlier Breps) still advance the offsets.
    size_t surfCPOffset = 0, surfUKnotOffset = 0, surfVKnotOffset = 0,
           surfWeightOffset = 0;   // NURBS packing offsets
    size_t nurbFace = 0, cylFace = 0, planeFace = 0, coneFace = 0,
           sphereFace = 0, torusFace = 0;   // per-type element counters
    auto mkAx3 = [](const GfVec3d& o, const GfVec3d& ax, const GfVec3d& rd) {
        return gp_Ax3(gp_Pnt(o[0], o[1], o[2]),
                      gp_Dir(ax[0], ax[1], ax[2]),
                      gp_Dir(rd[0], rd[1], rd[2]));
    };

    for (size_t faceIdx = 0;
         faceIdx < faceStart + numFaces &&
         faceIdx < data.faceSurfaceType.size(); ++faceIdx) {
        const TfToken& stype = data.faceSurfaceType[faceIdx];
        Handle(Geom_Surface) surf;

        if (stype.GetString() == "BrepSurfaceCylinderAPI") {
            if (cylFace < data.surfaceCylinderRadius.size() &&
                cylFace < data.surfaceCylinderOrigin.size()) {
                const GfVec3d& o  = data.surfaceCylinderOrigin[cylFace];
                const GfVec3d& ax = data.surfaceCylinderAxis[cylFace];
                const GfVec3d& rd = data.surfaceCylinderRefDir[cylFace];
                gp_Ax3 ax3(gp_Pnt(o[0], o[1], o[2]),
                           gp_Dir(ax[0], ax[1], ax[2]),
                           gp_Dir(rd[0], rd[1], rd[2]));
                surf = new Geom_CylindricalSurface(
                    ax3, data.surfaceCylinderRadius[cylFace]);
            }
            ++cylFace;
        } else if (stype.GetString() == "BrepSurfacePlaneAPI") {
            if (planeFace < data.surfacePlaneOrigin.size()) {
                surf = new Geom_Plane(mkAx3(data.surfacePlaneOrigin[planeFace],
                    data.surfacePlaneAxis[planeFace],
                    data.surfacePlaneRefDir[planeFace]));
            }
            ++planeFace;
        } else if (stype.GetString() == "BrepSurfaceConeAPI") {
            if (coneFace < data.surfaceConeRadius.size()) {
                surf = new Geom_ConicalSurface(
                    mkAx3(data.surfaceConeOrigin[coneFace],
                          data.surfaceConeAxis[coneFace],
                          data.surfaceConeRefDir[coneFace]),
                    data.surfaceConeSemiAngle[coneFace],
                    data.surfaceConeRadius[coneFace]);
            }
            ++coneFace;
        } else if (stype.GetString() == "BrepSurfaceSphereAPI") {
            if (sphereFace < data.surfaceSphereRadius.size()) {
                surf = new Geom_SphericalSurface(
                    mkAx3(data.surfaceSphereCenter[sphereFace],
                          data.surfaceSphereAxis[sphereFace],
                          data.surfaceSphereRefDir[sphereFace]),
                    data.surfaceSphereRadius[sphereFace]);
            }
            ++sphereFace;
        } else if (stype.GetString() == "BrepSurfaceTorusAPI") {
            if (torusFace < data.surfaceTorusMajorRadius.size()) {
                surf = new Geom_ToroidalSurface(
                    mkAx3(data.surfaceTorusOrigin[torusFace],
                          data.surfaceTorusAxis[torusFace],
                          data.surfaceTorusRefDir[torusFace]),
                    data.surfaceTorusMajorRadius[torusFace],
                    data.surfaceTorusMinorRadius[torusFace]);
            }
            ++torusFace;
        } else {
            // BrepSurfaceNurbAPI (default)
            if (nurbFace < data.surfaceUVertexCount.size()) {
                int uCnt = data.surfaceUVertexCount[nurbFace];
                int vCnt = data.surfaceVVertexCount[nurbFace];
                int uOrd = data.surfaceUOrder[nurbFace];
                int vOrd = data.surfaceVOrder[nurbFace];
                int numUKnots = uCnt + uOrd, numVKnots = vCnt + vOrd;
                int numCPs = uCnt * vCnt;
                const GfVec3d* cps = data.surfaceControlVertices.cdata() + surfCPOffset;
                const double* uKnots = data.surfaceUKnots.cdata() + surfUKnotOffset;
                const double* vKnots = data.surfaceVKnots.cdata() + surfVKnotOffset;
                const double* weights = (!data.surfaceWeights.empty())
                    ? data.surfaceWeights.cdata() + surfWeightOffset : nullptr;
                int numW = (!data.surfaceWeights.empty()) ? numCPs : 0;
                surf = _MakeBSplineSurface(cps, uCnt, vCnt, uOrd, vOrd,
                    uKnots, numUKnots, vKnots, numVKnots, weights, numW);
                surfCPOffset += numCPs; surfUKnotOffset += numUKnots;
                surfVKnotOffset += numVKnots; surfWeightOffset += numCPs;
            }
            ++nurbFace;
        }

        if (faceIdx >= faceStart) surfaces.push_back(surf);
    }

    // Build faces as untrimmed NURBS surfaces.
    //
    // When BrepCurveUvNurbAPI data is absent (no pcurves), we build untrimmed
    // faces from natural surface parameter bounds. Faces with multiple loops
    // (indicating trim curves we can't reconstruct) are skipped — they are
    // typically planar end-caps that would render as oversized rectangles.
    //
    // TODO: When BrepCurveUvNurbAPI data IS available, reconstruct full
    // edge topology with pcurves for proper trimmed tessellation.
    bool hasTrimCurves = !data.trimCurveControlVertices.empty();

    std::vector<TopoDS_Face> faces;
    std::vector<bool> faceNeedsFlip;
    faces.reserve(numFaces);
    faceNeedsFlip.reserve(numFaces);

    // Orient each face from the authored radial-edge data. "opposite" means the
    // solid-exterior (outward) normal points against the surface's natural
    // normal, so mark the face REVERSED and let the tessellator derive
    // winding/normals from face.Orientation(). Set absolutely (not a
    // conditional flip): face construction (ShapeFix_Face / MakeFace-with-wires)
    // may itself leave the face REVERSED, and the authored data is the single
    // source of truth.
    //
    // The schema requires the void-included form (issue #68): a closed solid
    // counts its infinite exterior void region, with faceuses grouped by shell
    // and an explicit faceuse:faceIndex. Two such layouts occur from CAD
    // producers (the legacy single-solidRegion interleaved layout with no void
    // region is no longer accepted):
    //  (B) Separate voidRegion + solidRegion shells with faceuses grouped by
    //      shell plus an explicit faceuse:faceIndex map (CAD producers, e.g.
    //      hoops-converter). Here 2*fi straddles both shells and mis-orients
    //      half the faces, collapsing closed solids to ~0 signed volume. Use
    //      the faceuse bounding the VOID region's shell (the solid's exterior
    //      side) as the outward reference, so the same opposite->REVERSED rule
    //      as (A) applies and normals point outward.
    //  (C) A void-ONLY body (voidRegion with no solidRegion -- e.g. CATIA
    //      solids via hoops-converter). Faceuses are interleaved coedge pairs,
    //      but the producer's pair ORDER varies between bodies, so the fixed
    //      2*fi pick (A) flips ('opposite','same')-first bodies entirely
    //      inward. Pick by CONTENT: the 'same' coedge of each pair is the
    //      outward (FORWARD) reference; REVERSE only when both coedges of the
    //      face are 'opposite'.
    std::vector<int> faceOutwardOpposite(numFaces, -1);   // -1 = unknown
    {
        bool hasVoid = false, hasSolid = false;
        for (size_t ri = regionStart; ri < regionStart + numRegions &&
                                      ri < data.regionType.size(); ++ri) {
            if (data.regionType[ri] == _tokens->voidRegion)  hasVoid = true;
            if (data.regionType[ri] == _tokens->solidRegion) hasSolid = true;
        }
        if (hasVoid && hasSolid && !data.faceuseFaceIndex.empty()) {
            // Layout (B): walk this Brep's shells and record the outward
            // orientation for each face from the faceuse bounding the voidRegion
            // shell (the solid's exterior side).
            size_t fu = faceuseStart, shellIdx = shellStart;
            for (size_t ri = regionStart; ri < regionStart + numRegions; ++ri) {
                const bool isVoid = ri < data.regionType.size() &&
                                    data.regionType[ri] == _tokens->voidRegion;
                const size_t nShells = ri < data.regionShellCount.size()
                                       ? data.regionShellCount[ri] : 0;
                for (size_t s = 0; s < nShells; ++s, ++shellIdx) {
                    const size_t nfu = shellIdx < data.shellFaceuseCount.size()
                                       ? data.shellFaceuseCount[shellIdx] : 0;
                    for (size_t j = 0; j < nfu; ++j, ++fu) {
                        if (!isVoid || fu >= data.faceuseFaceIndex.size())
                            continue;
                        const size_t fl =
                            (size_t)data.faceuseFaceIndex[fu] - faceStart;
                        if (fl < numFaces)
                            faceOutwardOpposite[fl] =
                                (fu < data.faceuseOrientationType.size() &&
                                 data.faceuseOrientationType[fu] ==
                                     _tokens->opposite) ? 1 : 0;
                    }
                }
            }
        } else if (hasVoid && !hasSolid) {
            // Layout (C): void-only body -- pick each face's outward
            // orientation by CONTENT from its interleaved coedge pair, since
            // the producer's pair order varies. Default to the 'same' coedge
            // as FORWARD; REVERSE only when both coedges are 'opposite'.
            for (size_t fi = 0; fi < numFaces; ++fi) {
                const size_t f0 = faceuseStart + 2 * fi, f1 = f0 + 1;
                if (f1 >= data.faceuseOrientationType.size()) break;
                if (!data.faceuseFaceIndex.empty() &&
                    f1 < data.faceuseFaceIndex.size() &&
                    data.faceuseFaceIndex[f0] != data.faceuseFaceIndex[f1])
                    continue;   // {f0,f1} not this face's interleaved pair
                faceOutwardOpposite[fi] =
                    (data.faceuseOrientationType[f0] == _tokens->opposite &&
                     data.faceuseOrientationType[f1] == _tokens->opposite)
                        ? 1 : 0;
            }
        }
    }
    bool warnedMissingVoid = false;
    auto applyFaceuseOrientation = [&](TopoDS_Face face, size_t fi) {
        // Layout (B)/(C) above resolved each face's outward side from its
        // void-region faceuse. The legacy single-solidRegion interleaved layout
        // (2*fi, no void region) is no longer accepted (issue #68); a face that
        // reaches here falls back to the surface's natural normal with a
        // one-time warning.
        bool outwardOpposite = false;
        if (fi < faceOutwardOpposite.size() && faceOutwardOpposite[fi] >= 0) {
            outwardOpposite = (faceOutwardOpposite[fi] == 1);    // (B)/(C)
        } else if (!warnedMissingVoid) {
            TF_WARN("hdOcct: a Brep face has no void-region faceuse; expected "
                    "the void-included BrepArray form (regionCount counts the "
                    "infinite void region). Orienting from the natural normal.");
            warnedMissingVoid = true;
        }
        face.Orientation(outwardOpposite ? TopAbs_REVERSED : TopAbs_FORWARD);
        return face;
    };

    if (!hasTrimCurves) {
        // Surface-only path: trim each face with its 3D edge curves.
        // Build the 3D curve for every edge, dispatching on edge:curveType
        // (NURBS / line / circle / ellipse), each packed per curve type.
        // Analytic surfaces project robustly here (the OCCT STEP-import
        // pattern), unlike the BSpline case where 3D-wire trimming under-meshes.
        auto mkAx2 = [](const GfVec3d& c, const GfVec3d& n, const GfVec3d& vx) {
            return gp_Ax2(gp_Pnt(c[0], c[1], c[2]),
                          gp_Dir(n[0], n[1], n[2]),
                          gp_Dir(vx[0], vx[1], vx[2]));
        };
        size_t numEdgesTotal = data.edgeCurveType.size();
        std::vector<Handle(Geom_Curve)> edge3dCurves(numEdgesTotal);
        std::vector<bool> edgeIsAnalytic(numEdgesTotal, false);
        {
            size_t eCP = 0, eKnot = 0, eW = 0;   // NURBS packing offsets
            size_t nurbE = 0, lineE = 0, circE = 0, ellE = 0;
            for (size_t ei = 0; ei < numEdgesTotal; ++ei) {
                const std::string ct = data.edgeCurveType[ei].GetString();
                try {
                if (ct == "BrepCurve3dLineAPI") {
                    edgeIsAnalytic[ei] = true;
                    if (lineE < data.edgeLineOrigin.size() &&
                        lineE < data.edgeLineDirection.size()) {
                        const GfVec3d& o = data.edgeLineOrigin[lineE];
                        const GfVec3d& d = data.edgeLineDirection[lineE];
                        edge3dCurves[ei] = new Geom_Line(
                            gp_Pnt(o[0], o[1], o[2]), gp_Dir(d[0], d[1], d[2]));
                    }
                    ++lineE;
                } else if (ct == "BrepCurve3dCircleAPI") {
                    edgeIsAnalytic[ei] = true;
                    if (circE < data.edgeCircleRadius.size()) {
                        edge3dCurves[ei] = new Geom_Circle(
                            mkAx2(data.edgeCircleCenter[circE],
                                  data.edgeCircleAxis[circE],
                                  data.edgeCircleRefDir[circE]),
                            data.edgeCircleRadius[circE]);
                    }
                    ++circE;
                } else if (ct == "BrepCurve3dEllipseAPI") {
                    edgeIsAnalytic[ei] = true;
                    if (ellE < data.edgeEllipseXRadius.size()) {
                        double xr = data.edgeEllipseXRadius[ellE];
                        double yr = data.edgeEllipseYRadius[ellE];
                        gp_Ax2 ax = mkAx2(data.edgeEllipseCenter[ellE],
                                          data.edgeEllipseAxis[ellE],
                                          data.edgeEllipseRefDir[ellE]);
                        // OCCT requires major >= minor; rotate frame if not.
                        if (xr >= yr) {
                            edge3dCurves[ei] = new Geom_Ellipse(ax, xr, yr);
                        } else {
                            ax.Rotate(ax.Axis(), 1.5707963267948966);
                            edge3dCurves[ei] = new Geom_Ellipse(ax, yr, xr);
                        }
                    }
                    ++ellE;
                } else {
                    // BrepCurve3dNurbAPI (default)
                    if (nurbE < data.edgeCurveVertexCount.size()) {
                        int ncp = data.edgeCurveVertexCount[nurbE];
                        int ord = data.edgeCurveOrder[nurbE];
                        int nk = ncp + ord;
                        const GfVec3d* cps =
                            data.edgeCurveControlVertices.cdata() + eCP;
                        const double* kn = data.edgeCurveKnots.cdata() + eKnot;
                        const double* w = (!data.edgeCurveWeights.empty())
                            ? data.edgeCurveWeights.cdata() + eW : nullptr;
                        int nw = (!data.edgeCurveWeights.empty()) ? ncp : 0;
                        edge3dCurves[ei] = _MakeBSplineCurve(
                            cps, ncp, ord, kn, nk, w, nw);
                        eCP += ncp; eKnot += nk; eW += ncp;
                    }
                    ++nurbE;
                }
                } catch (const Standard_Failure&) {
                    // Malformed edge curve: leave it null; the face build skips.
                }
            }
        }

        for (size_t fi = 0; fi < numFaces; ++fi) {
            if (fi >= surfaces.size()) break;
            const auto& surface = surfaces[fi];
            if (surface.IsNull()) continue;

            const bool analyticSurf =
                surface->IsKind(STANDARD_TYPE(Geom_ElementarySurface));

            size_t faceIdx = faceStart + fi;
            size_t numLoops = (faceIdx < data.faceLoopCount.size())
                ? data.faceLoopCount[faceIdx] : 0;

            // Loop + edgeuse start indices for this face.
            size_t loopStartIdx = 0;
            for (size_t f = 0; f < faceIdx; ++f)
                if (f < data.faceLoopCount.size())
                    loopStartIdx += data.faceLoopCount[f];
            size_t euStartIdx = 0;
            for (size_t l = 0; l < loopStartIdx; ++l)
                if (l < data.loopEdgeuseCount.size())
                    euStartIdx += data.loopEdgeuseCount[l];

            // Build a trimmed face from the loops' 3D edge wires.
            //
            // Analytic (elementary) surfaces use the full path for any loop
            // count: edges bounded by edge:range, exact pcurve projection, and a
            // validity check. NURBS surfaces keep legacy behaviour — only
            // multi-loop faces are wire-trimmed (without pcurve projection), and
            // single-loop faces fall through to natural bounds — because the
            // authored curveUv path is the correct route for trimmed NURBS.
            const bool attemptTrim = analyticSurf || numLoops > 1;

            TopoDS_Face finalFace;
            bool haveFace = false;

            if (attemptTrim) {
              try {
                bool built = false, ok = true;
                TopoDS_Face trimmedFace;
                size_t currentEU = euStartIdx;
                for (size_t li = 0; li < numLoops && ok; ++li) {
                    size_t globalLoop = loopStartIdx + li;
                    if (globalLoop >= data.loopEdgeuseCount.size()) {
                        ok = false; break;
                    }
                    size_t numEU = data.loopEdgeuseCount[globalLoop];
                    if (numEU == 0) { ok = false; break; }

                    BRepBuilderAPI_MakeWire wireMaker;
                    for (size_t eu = 0; eu < numEU; ++eu) {
                        size_t euIdx = currentEU + eu;
                        if (euIdx >= data.edgeuseEdgeIndex.size()) {
                            ok = false; break;
                        }
                        size_t edgeIdx = data.edgeuseEdgeIndex[euIdx];
                        if (edgeIdx >= edge3dCurves.size() ||
                            edge3dCurves[edgeIdx].IsNull()) {
                            ok = false; break;
                        }
                        // Analytic curves are infinite/periodic — bound them by
                        // the authored edge:range parameter interval.
                        TopoDS_Edge edge;
                        if (edgeIsAnalytic[edgeIdx] &&
                            2 * edgeIdx + 1 < data.edgeRange.size()) {
                            BRepBuilderAPI_MakeEdge em(edge3dCurves[edgeIdx],
                                data.edgeRange[2 * edgeIdx],
                                data.edgeRange[2 * edgeIdx + 1]);
                            if (!em.IsDone()) { ok = false; break; }
                            edge = em.Edge();
                        } else {
                            BRepBuilderAPI_MakeEdge em(edge3dCurves[edgeIdx]);
                            if (!em.IsDone()) { ok = false; break; }
                            edge = em.Edge();
                        }
                        // Attach an exact pcurve (3D->2D projection) so curved
                        // analytic faces mesh. NURBS keeps legacy behaviour.
                        if (analyticSurf) {
                            Standard_Real f, l;
                            Handle(Geom_Curve) c3 =
                                BRep_Tool::Curve(edge, f, l);
                            if (!c3.IsNull()) {
                                Handle(Geom2d_Curve) pc =
                                    GeomProjLib::Curve2d(c3, f, l, surface);
                                if (!pc.IsNull()) {
                                    BRep_Builder bb;
                                    bb.UpdateEdge(edge, pc, surface,
                                        TopLoc_Location(),
                                        data.intersectTol3d);
                                }
                            }
                        }
                        if (euIdx < data.edgeuseOrientationType.size() &&
                            data.edgeuseOrientationType[euIdx] ==
                                _tokens->opposite)
                            edge.Reverse();
                        wireMaker.Add(edge);
                    }
                    currentEU += numEU;
                    if (!ok || !wireMaker.IsDone()) { ok = false; break; }
                    TopoDS_Wire wire = wireMaker.Wire();

                    if (li == 0) {
                        BRepBuilderAPI_MakeFace faceMaker(surface, wire,
                                                          Standard_True);
                        if (!faceMaker.IsDone()) { ok = false; break; }
                        trimmedFace = faceMaker.Face();
                        built = true;
                    } else {
                        wire.Reverse();  // inner loops = holes
                        BRepBuilderAPI_MakeFace faceMaker(trimmedFace, wire);
                        if (!faceMaker.IsDone()) { ok = false; break; }
                        trimmedFace = faceMaker.Face();
                    }
                }

                if (built && ok && !trimmedFace.IsNull()) {
                    // Pole-bearing surfaces (sphere/torus) need the natural seam
                    // + degenerate pole edges added so a cap/great-circle wire
                    // closes.
                    bool poleSurface =
                        surface->IsKind(STANDARD_TYPE(Geom_SphericalSurface)) ||
                        surface->IsKind(STANDARD_TYPE(Geom_ToroidalSurface));
                    ShapeFix_Face fix(trimmedFace);
                    fix.FixAddNaturalBoundMode() =
                        (analyticSurf && poleSurface) ? Standard_True
                                                      : Standard_False;
                    fix.FixWireMode() = Standard_True;
                    fix.Perform();
                    trimmedFace = fix.Face();
                    BRepLib::SameParameter(trimmedFace, data.intersectTol3d);
                    // Analytic faces are validity-checked so invalid singular
                    // caps are skipped cleanly; NURBS keeps legacy behaviour.
                    if (!analyticSurf ||
                        BRepCheck_Analyzer(trimmedFace).IsValid()) {
                        finalFace = trimmedFace;
                        haveFace = true;
                    }
                }
              } catch (const Standard_Failure&) {
                  haveFace = false;   // never let an OCCT throw crash tessellation
              }
            }

            // Parametric fallback for analytic faces whose 3D-edge wire cannot
            // bound the patch — pole-enclosing or seam-encoded faces, e.g. a full
            // sphere whose only edge is a pole-to-pole meridian seam, a spherical
            // cap or apex cone that includes a pole. Build the face from the
            // authored face:range UV bounds; OCCT supplies the seam + degenerate
            // pole edges that a wire alone cannot express. (Common in real CAD:
            // producers trim analytic faces with 3D edges and no curveUv.)
            if (!haveFace && analyticSurf &&
                2 * faceIdx + 1 < data.faceRange.size()) {
                const GfVec2d& uvLo = data.faceRange[2 * faceIdx];
                const GfVec2d& uvHi = data.faceRange[2 * faceIdx + 1];
                if (uvHi[0] > uvLo[0] && uvHi[1] > uvLo[1]) {
                    try {
                        BRepBuilderAPI_MakeFace pf(surface, uvLo[0], uvHi[0],
                            uvLo[1], uvHi[1], data.intersectTol3d);
                        if (pf.IsDone()) {
                            ShapeFix_Face fx(pf.Face());
                            fx.Perform();
                            TopoDS_Face pff = fx.Face();
                            if (BRepCheck_Analyzer(pff).IsValid()) {
                                finalFace = pff;
                                haveFace = true;
                            }
                        }
                    } catch (const Standard_Failure&) {}
                }
            }

            // Untrimmed fallback: single-loop NURBS faces (legacy) and faces
            // with no boundary loops fall back to natural surface bounds. Faces
            // that had edges but failed to trim are skipped (no garbage geometry).
            if (!haveFace && (!attemptTrim || numLoops == 0)) {
                try {
                    BRepBuilderAPI_MakeFace faceMaker(
                        surface, data.intersectTol3d);
                    if (faceMaker.IsDone()) {
                        finalFace = faceMaker.Face();
                        haveFace = true;
                    }
                } catch (const Standard_Failure&) {}
            }
            if (haveFace) {
                faces.push_back(applyFaceuseOrientation(finalFace, fi));
                faceNeedsFlip.push_back(false);
            }
        }
    } else {
        // Full topology path with pcurves available
        size_t numEdges = data.edgeCurveVertexCount.size();
        size_t edgeCPOffset = 0;
        size_t edgeKnotOffset = 0;
        size_t edgeWeightOffset = 0;

        std::vector<TopoDS_Edge> edges(numEdges);

        for (size_t ei = 0; ei < numEdges; ++ei) {
            int numCPs = data.edgeCurveVertexCount[ei];
            int order = data.edgeCurveOrder[ei];
            int numKnots = numCPs + order;

            const GfVec3d* cps = data.edgeCurveControlVertices.cdata()
                                 + edgeCPOffset;
            const double* knots = data.edgeCurveKnots.cdata() + edgeKnotOffset;
            const double* weights = (!data.edgeCurveWeights.empty())
                ? data.edgeCurveWeights.cdata() + edgeWeightOffset : nullptr;
            int numW = (!data.edgeCurveWeights.empty()) ? numCPs : 0;

            auto curve = _MakeBSplineCurve(cps, numCPs, order, knots, numKnots,
                                           weights, numW);

            if (!curve.IsNull()) {
                auto vertIdx = data.edgeVertexIndices[ei];
                gp_Pnt p1(data.vertexPositions[vertIdx[0]][0],
                          data.vertexPositions[vertIdx[0]][1],
                          data.vertexPositions[vertIdx[0]][2]);
                gp_Pnt p2(data.vertexPositions[vertIdx[1]][0],
                          data.vertexPositions[vertIdx[1]][1],
                          data.vertexPositions[vertIdx[1]][2]);

                double paramMin = data.edgeRange[ei * 2];
                double paramMax = data.edgeRange[ei * 2 + 1];

                BRepBuilderAPI_MakeEdge edgeMaker(curve, p1, p2,
                                                   paramMin, paramMax);
                if (edgeMaker.IsDone()) {
                    edges[ei] = edgeMaker.Edge();
                } else {
                    BRepBuilderAPI_MakeEdge fallback(p1, p2);
                    if (fallback.IsDone()) {
                        edges[ei] = fallback.Edge();
                    }
                }
            }

            edgeCPOffset += numCPs;
            edgeKnotOffset += numKnots;
            edgeWeightOffset += numCPs;
        }

        // Per-edgeuse offsets into the packed curveUv (trim) arrays.
        // curveUv is authored one 2D pcurve per edgeuse, in global edgeuse
        // order (schema: "packed in order used by edgeuses").
        const size_t numTrim = data.trimCurveVertexCount.size();
        std::vector<size_t> trimCPOff(numTrim + 1, 0), trimKOff(numTrim + 1, 0);
        for (size_t i = 0; i < numTrim; ++i) {
            int tvc = (int)data.trimCurveVertexCount[i];
            int tord = (int)data.trimCurveOrder[i];
            trimCPOff[i + 1] = trimCPOff[i] + tvc;
            trimKOff[i + 1]  = trimKOff[i] + tvc + tord;
        }
        const bool useTrim = (numTrim == data.edgeuseEdgeIndex.size()) &&
                             !data.trimCurveControlVertices.empty();

        // Build faces with edge loops + pcurves
        size_t loopStart = 0;
        for (size_t i = 0; i < faceStart && i < data.faceLoopCount.size(); ++i) {
            loopStart += data.faceLoopCount[i];
        }

        size_t edgeuseStart_local = 0;
        for (size_t i = 0; i < loopStart
             && i < data.loopEdgeuseCount.size(); ++i) {
            edgeuseStart_local += data.loopEdgeuseCount[i];
        }

        size_t currentLoop = loopStart;
        size_t currentEdgeuse = edgeuseStart_local;

        for (size_t fi = 0; fi < numFaces; ++fi) {
            size_t faceIdx = faceStart + fi;
            if (faceIdx >= data.faceLoopCount.size()) break;

            int numLoops = data.faceLoopCount[faceIdx];

            Handle(Geom_Surface) surface =
                (fi < surfaces.size()) ? surfaces[fi]
                                       : Handle(Geom_Surface)();

            if (surface.IsNull()) {
                for (int li = 0; li < numLoops; ++li) {
                    if (currentLoop < data.loopEdgeuseCount.size()) {
                        currentEdgeuse += data.loopEdgeuseCount[currentLoop];
                    }
                    currentLoop++;
                }
                continue;
            }

            bool faceOk = true;
            std::vector<TopoDS_Wire> loopWires;   // [0] = outer, rest = holes

            for (int li = 0; li < numLoops; ++li) {
                if (currentLoop >= data.loopEdgeuseCount.size()) {
                    faceOk = false; break;
                }
                int numEdgeuses = data.loopEdgeuseCount[currentLoop];
                size_t euStart = currentEdgeuse;
                // Advance counters deterministically so a build failure on one
                // loop cannot desync subsequent faces.
                currentEdgeuse += numEdgeuses;
                currentLoop++;
                if (!faceOk || numEdgeuses == 0) { loopWires.push_back(TopoDS_Wire()); continue; }

                BRepBuilderAPI_MakeWire wireMaker;
                bool wireOk = true;
                for (int eu = 0; eu < numEdgeuses; ++eu) {
                    size_t euIdx = euStart + eu;

                    // Preferred: build the edge directly from its authored 2D
                    // pcurve on the surface (no projection of a 3D wire, which
                    // is what malformed the trimmed faces — Finding #9).
                    if (useTrim && euIdx < numTrim) {
                        int tvc = (int)data.trimCurveVertexCount[euIdx];
                        int tord = (int)data.trimCurveOrder[euIdx];
                        const GfVec2d* cp =
                            data.trimCurveControlVertices.cdata() + trimCPOff[euIdx];
                        const double* kn =
                            data.trimCurveKnots.cdata() + trimKOff[euIdx];
                        const double* wt = data.trimCurveWeights.empty()
                            ? nullptr
                            : data.trimCurveWeights.cdata() + trimCPOff[euIdx];
                        auto c2d = _MakeBSplineCurve2d(
                            cp, tvc, tord, kn, tvc + tord, wt, wt ? tvc : 0);
                        if (!c2d.IsNull()) {
                            double t0 = kn[0];
                            double t1 = kn[tvc + tord - 1];
                            BRepBuilderAPI_MakeEdge em(c2d, surface, t0, t1);
                            if (em.IsDone()) { wireMaker.Add(em.Edge()); continue; }
                        }
                        // fall through to the 3D-edge path on any failure
                    }

                    if (euIdx >= data.edgeuseEdgeIndex.size()) { wireOk = false; break; }
                    unsigned int edgeIdx = data.edgeuseEdgeIndex[euIdx];
                    if (edgeIdx < edges.size() && !edges[edgeIdx].IsNull()) {
                        TopoDS_Edge edge = edges[edgeIdx];
                        if (euIdx < data.edgeuseOrientationType.size() &&
                            data.edgeuseOrientationType[euIdx] == _tokens->opposite) {
                            edge.Reverse();
                        }
                        wireMaker.Add(edge);
                    }
                }

                if (!wireOk || !wireMaker.IsDone()) {
                    faceOk = false; loopWires.push_back(TopoDS_Wire()); continue;
                }
                loopWires.push_back(wireMaker.Wire());
            }

            if (faceOk && !loopWires.empty() && !loopWires[0].IsNull()) {
                // Build the face FROM its outer wire (Inside=true bounds a
                // finite region); add the remaining loops as holes (reversed).
                // NOT MakeFace(surface)+Add(outer): that treats the outer wire
                // as a hole in the surface's full natural rectangle.
                BRepBuilderAPI_MakeFace faceMaker(surface, loopWires[0],
                                                  Standard_True);
                for (size_t k = 1; k < loopWires.size(); ++k) {
                    if (loopWires[k].IsNull()) continue;
                    TopoDS_Wire hole = loopWires[k];
                    hole.Reverse();               // hole runs opposite the outer
                    faceMaker.Add(hole);
                }
                if (!faceMaker.IsDone()) faceOk = false;
                if (faceOk) {
                    TopoDS_Face face = faceMaker.Face();
                    // pcurve-built edges carry no 3D curve and leave
                    // SameParameter unset; synthesize/reconcile both before
                    // the face is handed to the mesher.
                    BRepLib::BuildCurves3d(face, data.intersectTol3d);
                    BRepLib::SameParameter(face, data.intersectTol3d,
                                           Standard_True);
                    faces.push_back(applyFaceuseOrientation(face, fi));
                }
            }
        }
    }

    // Assemble faces into a compound (preserving face order/orientation).
    // Don't sew — untrimmed faces aren't connected, and sewing can flip
    // face orientations which breaks normal handling.
    if (faces.empty()) {
        TF_WARN("Brep %zu: no faces built", brepIndex);
        return TopoDS_Shape();
    }

    TopoDS_Compound compound;
    BRep_Builder compoundBuilder;
    compoundBuilder.MakeCompound(compound);
    for (const auto& face : faces) {
        compoundBuilder.Add(compound, face);
    }

    return compound;
}

std::vector<TopoDS_Shape>
UsdSolidBrepBuilder::Build(const UsdPrim& brepArrayPrim) const
{
    _BrepData data;
    if (!_ReadBrepData(brepArrayPrim, &data)) {
        TF_WARN("Failed to read BrepArray data from prim '%s'",
                 brepArrayPrim.GetPath().GetText());
        return {};
    }

    size_t numBreps = data.regionCount.size();
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(numBreps);

    for (size_t i = 0; i < numBreps; ++i) {
        shapes.push_back(_BuildSingleBrep(data, i));
    }

    return shapes;
}

std::optional<TopoDS_Shape>
UsdSolidBrepBuilder::BuildSingleBrep(
    const UsdPrim& brepArrayPrim,
    size_t brepIndex) const
{
    _BrepData data;
    if (!_ReadBrepData(brepArrayPrim, &data)) {
        return std::nullopt;
    }

    if (brepIndex >= data.regionCount.size()) {
        TF_WARN("Brep index %zu out of range (have %zu Breps)",
                 brepIndex, data.regionCount.size());
        return std::nullopt;
    }

    auto shape = _BuildSingleBrep(data, brepIndex);
    if (shape.IsNull()) return std::nullopt;
    return shape;
}

PXR_NAMESPACE_CLOSE_SCOPE

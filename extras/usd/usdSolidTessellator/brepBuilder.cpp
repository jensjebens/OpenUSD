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
#include <Geom2d_BSplineCurve.hxx>
#include <gp_Pnt.hxx>
#include <ShapeFix_Shape.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
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

    // Compute face start index: count faces before this Brep
    size_t faceStart = faceuseStart / 2;  // Approximate; needs proper offset chain

    // Build OCCT surfaces for each face
    std::vector<Handle(Geom_BSplineSurface)> surfaces;
    surfaces.reserve(numFaces);

    size_t surfCPOffset = 0;  // offset into surfaceControlVertices
    size_t surfUKnotOffset = 0;
    size_t surfVKnotOffset = 0;
    size_t surfWeightOffset = 0;

    // Skip surfaces before faceStart
    for (size_t i = 0; i < faceStart && i < data.surfaceUVertexCount.size(); ++i) {
        int uCnt = data.surfaceUVertexCount[i];
        int vCnt = data.surfaceVVertexCount[i];
        surfCPOffset += uCnt * vCnt;
        surfUKnotOffset += uCnt + data.surfaceUOrder[i];
        surfVKnotOffset += vCnt + data.surfaceVOrder[i];
        surfWeightOffset += uCnt * vCnt;
    }

    for (size_t fi = 0; fi < numFaces; ++fi) {
        size_t faceIdx = faceStart + fi;
        if (faceIdx >= data.surfaceUVertexCount.size()) break;

        int uCnt = data.surfaceUVertexCount[faceIdx];
        int vCnt = data.surfaceVVertexCount[faceIdx];
        int uOrd = data.surfaceUOrder[faceIdx];
        int vOrd = data.surfaceVOrder[faceIdx];
        int numUKnots = uCnt + uOrd;
        int numVKnots = vCnt + vOrd;
        int numCPs = uCnt * vCnt;

        const GfVec3d* cps = data.surfaceControlVertices.cdata() + surfCPOffset;
        const double* uKnots = data.surfaceUKnots.cdata() + surfUKnotOffset;
        const double* vKnots = data.surfaceVKnots.cdata() + surfVKnotOffset;
        const double* weights = (!data.surfaceWeights.empty())
            ? data.surfaceWeights.cdata() + surfWeightOffset : nullptr;
        int numW = (!data.surfaceWeights.empty()) ? numCPs : 0;

        auto surface = _MakeBSplineSurface(
            cps, uCnt, vCnt, uOrd, vOrd,
            uKnots, numUKnots, vKnots, numVKnots,
            weights, numW);
        surfaces.push_back(surface);

        surfCPOffset += numCPs;
        surfUKnotOffset += numUKnots;
        surfVKnotOffset += numVKnots;
        surfWeightOffset += numCPs;
    }

    // Build OCCT edges from 3D curves
    // Compute edge start for this Brep (simplified: assume sequential)
    size_t edgeStart = 0;  // TODO: proper offset from brep:edgeCount
    size_t numEdges = data.edgeCurveVertexCount.size();  // For single-Brep case

    size_t edgeCPOffset = 0;
    size_t edgeKnotOffset = 0;
    size_t edgeWeightOffset = 0;

    std::vector<TopoDS_Edge> edges;
    edges.reserve(numEdges);

    for (size_t ei = 0; ei < numEdges; ++ei) {
        int numCPs = data.edgeCurveVertexCount[ei];
        int order = data.edgeCurveOrder[ei];
        int numKnots = numCPs + order;

        const GfVec3d* cps = data.edgeCurveControlVertices.cdata() + edgeCPOffset;
        const double* knots = data.edgeCurveKnots.cdata() + edgeKnotOffset;
        const double* weights = (!data.edgeCurveWeights.empty())
            ? data.edgeCurveWeights.cdata() + edgeWeightOffset : nullptr;
        int numW = (!data.edgeCurveWeights.empty()) ? numCPs : 0;

        auto curve = _MakeBSplineCurve(cps, numCPs, order, knots, numKnots,
                                        weights, numW);

        if (!curve.IsNull()) {
            // Get vertex positions for edge endpoints
            auto vertIdx = data.edgeVertexIndices[ei];
            gp_Pnt p1(data.vertexPositions[vertIdx[0]][0],
                      data.vertexPositions[vertIdx[0]][1],
                      data.vertexPositions[vertIdx[0]][2]);
            gp_Pnt p2(data.vertexPositions[vertIdx[1]][0],
                      data.vertexPositions[vertIdx[1]][1],
                      data.vertexPositions[vertIdx[1]][2]);

            // Edge parameter range
            double paramMin = data.edgeRange[ei * 2];
            double paramMax = data.edgeRange[ei * 2 + 1];

            BRepBuilderAPI_MakeEdge edgeMaker(curve, p1, p2,
                                               paramMin, paramMax);
            if (edgeMaker.IsDone()) {
                edges.push_back(edgeMaker.Edge());
            } else {
                // Fallback: edge from endpoints only
                TopoDS_Vertex v1, v2;
                builder.MakeVertex(v1, p1, data.intersectTol3d);
                builder.MakeVertex(v2, p2, data.intersectTol3d);
                BRepBuilderAPI_MakeEdge fallback(p1, p2);
                if (fallback.IsDone()) {
                    edges.push_back(fallback.Edge());
                }
            }
        }

        edgeCPOffset += numCPs;
        edgeKnotOffset += numKnots;
        edgeWeightOffset += numCPs;
    }

    // Build faces from surfaces and edge loops
    std::vector<TopoDS_Face> faces;
    faces.reserve(numFaces);

    size_t loopStart = 0;
    // Skip loops before faceStart
    for (size_t i = 0; i < faceStart && i < data.faceLoopCount.size(); ++i) {
        loopStart += data.faceLoopCount[i];
    }

    size_t edgeuseStart_local = 0;
    for (size_t i = 0; i < loopStart && i < data.loopEdgeuseCount.size(); ++i) {
        edgeuseStart_local += data.loopEdgeuseCount[i];
    }

    size_t currentLoop = loopStart;
    size_t currentEdgeuse = edgeuseStart_local;

    for (size_t fi = 0; fi < numFaces; ++fi) {
        size_t faceIdx = faceStart + fi;
        if (faceIdx >= data.faceLoopCount.size()) break;

        int numLoops = data.faceLoopCount[faceIdx];

        // Get the surface for this face
        Handle(Geom_BSplineSurface) surface =
            (fi < surfaces.size()) ? surfaces[fi] : Handle(Geom_BSplineSurface)();

        if (surface.IsNull()) {
            // Skip face if no surface
            for (int li = 0; li < numLoops; ++li) {
                if (currentLoop < data.loopEdgeuseCount.size()) {
                    currentEdgeuse += data.loopEdgeuseCount[currentLoop];
                }
                currentLoop++;
            }
            continue;
        }

        // Build the face from outer wire (first loop) + inner wires
        BRepBuilderAPI_MakeFace faceMaker(surface, data.intersectTol3d);

        for (int li = 0; li < numLoops; ++li) {
            if (currentLoop >= data.loopEdgeuseCount.size()) break;
            int numEdgeuses = data.loopEdgeuseCount[currentLoop];

            if (numEdgeuses == 0) {
                // Degenerate loop (single vertex) — skip
                currentLoop++;
                continue;
            }

            BRepBuilderAPI_MakeWire wireMaker;
            for (int eu = 0; eu < numEdgeuses; ++eu) {
                size_t euIdx = currentEdgeuse + eu;
                if (euIdx >= data.edgeuseEdgeIndex.size()) break;

                unsigned int edgeIdx = data.edgeuseEdgeIndex[euIdx];
                if (edgeIdx < edges.size()) {
                    TopoDS_Edge edge = edges[edgeIdx];
                    // Reverse edge if edgeuse orientation is "opposite"
                    if (euIdx < data.edgeuseOrientationType.size() &&
                        data.edgeuseOrientationType[euIdx] == _tokens->opposite) {
                        edge.Reverse();
                    }
                    wireMaker.Add(edge);
                }
            }

            if (wireMaker.IsDone()) {
                TopoDS_Wire wire = wireMaker.Wire();
                if (li == 0) {
                    // Outer wire
                    faceMaker.Add(wire);
                } else {
                    // Inner wire (hole) — reversed
                    wire.Reverse();
                    faceMaker.Add(wire);
                }
            }

            currentEdgeuse += numEdgeuses;
            currentLoop++;
        }

        if (faceMaker.IsDone()) {
            faces.push_back(faceMaker.Face());
        }
    }

    // Assemble faces into a shell/solid using sewing
    if (faces.empty()) {
        TF_WARN("Brep %zu: no faces built", brepIndex);
        return TopoDS_Shape();
    }

    BRepBuilderAPI_Sewing sewing(data.intersectTol3d);
    for (const auto& face : faces) {
        sewing.Add(face);
    }
    sewing.Perform();

    TopoDS_Shape sewedShape = sewing.SewedShape();

    // Shape healing pass
    ShapeFix_Shape fixer(sewedShape);
    fixer.SetPrecision(data.intersectTol3d);
    fixer.Perform();

    return fixer.Shape();
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

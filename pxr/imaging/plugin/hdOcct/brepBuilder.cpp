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
#include <ShapeFix_Wire.hxx>
#include <ShapeFix_Face.hxx>
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
    } catch (Standard_Failure& e) {
        // Dump actual knot data for diagnosis
        std::string kstr;
        for (size_t i = 0; i < knotVals.size(); ++i) {
            char buf[32]; snprintf(buf, sizeof(buf), "%.4f(%d)", knotVals[i], mults[i]);
            if (!kstr.empty()) kstr += ", ";
            kstr += buf;
        }
        TF_WARN("Failed to construct BSplineCurve: degree=%d, poles=%d, "
                 "knots=%d [%s] reason: %s",
                 degree, numPts, (int)knotVals.size(), kstr.c_str(),
                 e.GetMessageString());
        return nullptr;
    } catch (...) {
        TF_WARN("Failed to construct BSplineCurve: degree=%d, poles=%d (unknown exception)",
                 degree, numPts);
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
    std::vector<Handle(Geom_BSplineSurface)> surfaces;
    surfaces.reserve(numFaces);

    size_t surfCPOffset = 0;
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

    if (!hasTrimCurves) {
        // Surface-only path: build faces, use 3D edge curves for trimming
        for (size_t fi = 0; fi < numFaces; ++fi) {
            if (fi >= surfaces.size()) break;
            const auto& surface = surfaces[fi];
            if (surface.IsNull()) continue;

            // Check if this face has multiple loops (needs trimming)
            size_t faceIdx = faceStart + fi;
            if (faceIdx < data.faceLoopCount.size() &&
                data.faceLoopCount[faceIdx] > 1) {
                // Multi-loop face: build trimmed face using 3D edge curves.
                if (data.edgeCurveControlVertices.empty() ||
                    data.edgeCurveVertexCount.empty()) {
                    continue;  // No 3D edge data — can't trim
                }

                // Compute loop start for this face
                size_t loopStartIdx = 0;
                for (size_t f = 0; f < faceIdx; ++f) {
                    if (f < data.faceLoopCount.size())
                        loopStartIdx += data.faceLoopCount[f];
                }

                // Compute edgeuse start for this face's loops
                size_t euStartIdx = 0;
                for (size_t l = 0; l < loopStartIdx; ++l) {
                    if (l < data.loopEdgeuseCount.size())
                        euStartIdx += data.loopEdgeuseCount[l];
                }

                size_t numLoops = data.faceLoopCount[faceIdx];
                bool success = true;
                TopoDS_Face trimmedFace;

                size_t currentEU = euStartIdx;
                for (size_t li = 0; li < numLoops && success; ++li) {
                    size_t globalLoop = loopStartIdx + li;
                    if (globalLoop >= data.loopEdgeuseCount.size()) {
                        success = false; break;
                    }
                    size_t numEU = data.loopEdgeuseCount[globalLoop];
                    if (numEU == 0) { currentEU += numEU; continue; }

                    // Build wire from edgeuses in this loop
                    BRepBuilderAPI_MakeWire wireMaker;
                    for (size_t eu = 0; eu < numEU; ++eu) {
                        size_t euIdx = currentEU + eu;
                        if (euIdx >= data.edgeuseEdgeIndex.size()) {
                            success = false; break;
                        }
                        size_t edgeIdx = data.edgeuseEdgeIndex[euIdx];
                        if (edgeIdx >= data.edgeCurveVertexCount.size()) {
                            success = false; break;
                        }

                        // Compute offset into edge curve arrays
                        size_t ecpOffset = 0, eknotOffset = 0, ewOffset = 0;
                        for (size_t e = 0; e < edgeIdx; ++e) {
                            ecpOffset += data.edgeCurveVertexCount[e];
                            eknotOffset += data.edgeCurveVertexCount[e] +
                                           data.edgeCurveOrder[e];
                            ewOffset += data.edgeCurveVertexCount[e];
                        }

                        int numCPs = data.edgeCurveVertexCount[edgeIdx];
                        int edgeOrder = data.edgeCurveOrder[edgeIdx];
                        int numKnots = numCPs + edgeOrder;

                        const GfVec3d* cps =
                            data.edgeCurveControlVertices.cdata() + ecpOffset;
                        const double* knots =
                            data.edgeCurveKnots.cdata() + eknotOffset;
                        const double* weights =
                            (!data.edgeCurveWeights.empty())
                            ? data.edgeCurveWeights.cdata() + ewOffset
                            : nullptr;
                        int numW = (!data.edgeCurveWeights.empty())
                                   ? numCPs : 0;

                        auto curve = _MakeBSplineCurve(
                            cps, numCPs, edgeOrder, knots, numKnots,
                            weights, numW);
                        if (curve.IsNull()) { success = false; break; }

                        // Check if edgeuse orientation is "opposite" → reverse
                        bool euReversed = false;
                        if (euIdx < data.edgeuseOrientationType.size()) {
                            euReversed = (data.edgeuseOrientationType[euIdx]
                                          == _tokens->opposite);
                        }

                        // Build edge from curve
                        BRepBuilderAPI_MakeEdge edgeMaker(curve);
                        if (!edgeMaker.IsDone()) { success = false; break; }
                        TopoDS_Edge edge = edgeMaker.Edge();
                        if (euReversed) edge.Reverse();
                        wireMaker.Add(edge);
                    }
                    currentEU += numEU;

                    if (!success || !wireMaker.IsDone()) {
                        success = false; break;
                    }
                    TopoDS_Wire wire = wireMaker.Wire();

                    if (li == 0) {
                        // First loop = outer boundary
                        BRepBuilderAPI_MakeFace faceMaker(surface, wire,
                                                         Standard_True);
                        if (!faceMaker.IsDone()) { success = false; break; }
                        trimmedFace = faceMaker.Face();
                    } else {
                        // Inner loops = holes
                        wire.Reverse();  // Inner wires must be reversed
                        BRepBuilderAPI_MakeFace faceMaker(trimmedFace, wire);
                        if (!faceMaker.IsDone()) { success = false; break; }
                        trimmedFace = faceMaker.Face();
                    }
                }

                if (success && !trimmedFace.IsNull()) {
                    // Ensure pcurves exist for BRepMesh to work
                    ShapeFix_Face fix(trimmedFace);
                    fix.FixAddNaturalBoundMode() = Standard_False;
                    fix.FixWireMode() = Standard_True;
                    fix.Perform();
                    trimmedFace = fix.Face();
                    // Apply faceuse orientation
                    size_t fuIdx = faceuseStart + fi * 2;  // outer faceuse
                    if (fuIdx < data.faceuseOrientationType.size() &&
                        data.faceuseOrientationType[fuIdx] == _tokens->opposite) {
                        trimmedFace.Reverse();
                    }
                    faces.push_back(trimmedFace);
                    faceNeedsFlip.push_back(false);
                }
                continue;
            }

            BRepBuilderAPI_MakeFace faceMaker(surface, data.intersectTol3d);
            if (faceMaker.IsDone()) {
                TopoDS_Face face = faceMaker.Face();
                // Apply faceuse orientation for correct winding
                size_t fuIdx = faceuseStart + fi * 2;  // outer faceuse
                if (fuIdx < data.faceuseOrientationType.size() &&
                    data.faceuseOrientationType[fuIdx] == _tokens->opposite) {
                    face.Reverse();
                }
                faces.push_back(face);
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

            Handle(Geom_BSplineSurface) surface =
                (fi < surfaces.size()) ? surfaces[fi]
                                       : Handle(Geom_BSplineSurface)();

            if (surface.IsNull()) {
                for (int li = 0; li < numLoops; ++li) {
                    if (currentLoop < data.loopEdgeuseCount.size()) {
                        currentEdgeuse += data.loopEdgeuseCount[currentLoop];
                    }
                    currentLoop++;
                }
                continue;
            }

            BRepBuilderAPI_MakeFace faceMaker(surface, data.intersectTol3d);

            for (int li = 0; li < numLoops; ++li) {
                if (currentLoop >= data.loopEdgeuseCount.size()) break;
                int numEdgeuses = data.loopEdgeuseCount[currentLoop];

                if (numEdgeuses == 0) {
                    currentLoop++;
                    continue;
                }

                BRepBuilderAPI_MakeWire wireMaker;
                for (int eu = 0; eu < numEdgeuses; ++eu) {
                    size_t euIdx = currentEdgeuse + eu;
                    if (euIdx >= data.edgeuseEdgeIndex.size()) break;

                    unsigned int edgeIdx = data.edgeuseEdgeIndex[euIdx];
                    if (edgeIdx < edges.size()
                        && !edges[edgeIdx].IsNull()) {
                        TopoDS_Edge edge = edges[edgeIdx];
                        if (euIdx < data.edgeuseOrientationType.size() &&
                            data.edgeuseOrientationType[euIdx]
                                == _tokens->opposite) {
                            edge.Reverse();
                        }
                        wireMaker.Add(edge);
                    }
                }

                if (wireMaker.IsDone()) {
                    TopoDS_Wire wire = wireMaker.Wire();
                    if (li == 0) {
                        faceMaker.Add(wire);
                    } else {
                        wire.Reverse();
                        faceMaker.Add(wire);
                    }
                }

                currentEdgeuse += numEdgeuses;
                currentLoop++;
            }

            if (faceMaker.IsDone()) {
                TopoDS_Face face = faceMaker.Face();
                // Apply faceuse orientation: "opposite" means the surface
                // natural normal opposes the shell outward direction — reverse
                // the face so OCCT's face.Orientation() == TopAbs_REVERSED,
                // which the tessellator uses for winding swap.
                size_t fuIdx = faceuseStart + fi * 2;  // outer faceuse
                if (fuIdx < data.faceuseOrientationType.size() &&
                    data.faceuseOrientationType[fuIdx] == _tokens->opposite) {
                    face.Reverse();
                }
                faces.push_back(face);
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

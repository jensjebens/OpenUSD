// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Tessellator implementation: wraps BrepBuilder + OCCT BRepMesh to produce
// triangle meshes from UsdSolid BrepArray prims.

#include "tessellator.h"
#include "brepBuilder.h"
#include "meshExporter.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xformable.h"

// OpenCascade meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Polygon3D.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopLoc_Location.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <Geom_Surface.hxx>
#include <Geom2d_Curve.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt2d.hxx>
#include <map>
#include <string>
#include <tuple>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <GeomLProp_SLProps.hxx>

PXR_NAMESPACE_OPEN_SCOPE

// --------------------------------------------------------------------------
// UsdSolidTessellator
// --------------------------------------------------------------------------

UsdSolidTessellator::UsdSolidTessellator() = default;
UsdSolidTessellator::~UsdSolidTessellator() = default;

namespace {

/// Extract triangulation from an OCCT shape after BRepMesh has been run.
UsdSolidTessellationResult _ExtractMesh(
    const TopoDS_Shape& shape,
    const UsdSolidTessellationParams& params,
    size_t brepIndex)
{
    UsdSolidTessellationResult result;
    result.success = false;

    if (shape.IsNull()) {
        result.errorMessage = "Null shape";
        return result;
    }

    // Run BRepMesh tessellation
    double deflection = params.linearDeflection;

    // If relative mode, compute deflection from bounding box
    if (params.relativeDeflection) {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (!box.IsVoid()) {
            double xmin, ymin, zmin, xmax, ymax, zmax;
            box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            double diag = std::sqrt(
                (xmax-xmin)*(xmax-xmin) +
                (ymax-ymin)*(ymax-ymin) +
                (zmax-zmin)*(zmax-zmin));
            deflection = diag * params.linearDeflection;
        }
    }

    BRepMesh_IncrementalMesh mesher(
        shape,
        deflection,
        Standard_False,  // not relative (we handle it above)
        params.angularDeflection,
        Standard_True    // parallel
    );

    if (!mesher.IsDone()) {
        result.errorMessage = "BRepMesh_IncrementalMesh failed";
        return result;
    }

    // Collect triangulation from all faces
    int totalVerts = 0;
    int totalTris = 0;
    int faceIndex = 0;

    // First pass: count totals
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(exp.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (!tri.IsNull()) {
            totalVerts += tri->NbNodes();
            totalTris += tri->NbTriangles();
        }
    }

    if (totalTris == 0) {
        result.errorMessage = "No triangles produced";
        return result;
    }

    // Allocate
    result.points.resize(totalVerts);
    result.faceVertexCounts.resize(totalTris);
    result.faceVertexIndices.resize(totalTris * 3);
    result.faceBrepIndices.resize(totalTris);
    result.faceSolidFaceIndices.resize(totalTris);

    if (params.computeNormals) {
        result.normals.resize(totalVerts);
    }
    if (params.computeUVs) {
        result.uvs.resize(totalVerts);
    }

    // Second pass: extract data
    int vertOffset = 0;
    int triOffset = 0;
    faceIndex = 0;

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(exp.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) {
            faceIndex++;
            continue;
        }

        const gp_Trsf& trsf = loc.Transformation();
        bool isReversed = (face.Orientation() == TopAbs_REVERSED);

        int nbNodes = tri->NbNodes();
        int nbTris = tri->NbTriangles();

        // Winding/normal flip is driven by the face's OCCT orientation,
        // which brepBuilder derives from the authored faceuse:orientationType
        // (the outward side of each face). REVERSED means the outward
        // direction is against the surface's natural normal.

        // Vertices
        for (int i = 1; i <= nbNodes; ++i) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            result.points[vertOffset + i - 1] = GfVec3d(p.X(), p.Y(), p.Z());
        }

        // Normals from parametric surface
        if (params.computeNormals && tri->HasUVNodes()) {
            BRepAdaptor_Surface surfAdaptor(face, Standard_True);
            bool flipNormal = isReversed;
            
            for (int i = 1; i <= nbNodes; ++i) {
                gp_Pnt2d uv = tri->UVNode(i);
                gp_Pnt pnt;
                gp_Vec du, dv;
                surfAdaptor.D1(uv.X(), uv.Y(), pnt, du, dv);
                gp_Vec normal = du.Crossed(dv);
                if (normal.Magnitude() > 1e-10) {
                    normal.Normalize();
                    if (flipNormal) {
                        normal.Reverse();
                    }
                    result.normals[vertOffset + i - 1] =
                        GfVec3f((float)normal.X(),
                                (float)normal.Y(),
                                (float)normal.Z());
                } else {
                    result.normals[vertOffset + i - 1] = GfVec3f(0, 0, 1);
                }
            }
        }

        // UVs
        if (params.computeUVs && tri->HasUVNodes()) {
            for (int i = 1; i <= nbNodes; ++i) {
                gp_Pnt2d uv = tri->UVNode(i);
                result.uvs[vertOffset + i - 1] =
                    GfVec2f((float)uv.X(), (float)uv.Y());
            }
        }

        // Triangles
        for (int i = 1; i <= nbTris; ++i) {
            int n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);

            // Winding: align each triangle with the shading normals (which
            // already encode the authored faceuse orientation). This also
            // corrects wire-rebuilt trimmed faces whose triangulation can
            // come out inverted even when orientation and normals are
            // correct. Fall back to the orientation flag when normals are
            // unavailable.
            bool swapWinding = isReversed;
            if (params.computeNormals && tri->HasUVNodes()) {
                const GfVec3d& pa = result.points[vertOffset + n1 - 1];
                const GfVec3d& pb = result.points[vertOffset + n2 - 1];
                const GfVec3d& pc = result.points[vertOffset + n3 - 1];
                const GfVec3d geomN = GfCross(pb - pa, pc - pa);
                const GfVec3f& sn = result.normals[vertOffset + n1 - 1];
                const GfVec3d shadeN(sn[0], sn[1], sn[2]);
                swapWinding = (GfDot(geomN, shadeN) < 0);
            }
            if (swapWinding) {
                std::swap(n1, n3);
            }

            result.faceVertexCounts[triOffset + i - 1] = 3;
            result.faceVertexIndices[(triOffset + i - 1) * 3 + 0] =
                vertOffset + n1 - 1;
            result.faceVertexIndices[(triOffset + i - 1) * 3 + 1] =
                vertOffset + n2 - 1;
            result.faceVertexIndices[(triOffset + i - 1) * 3 + 2] =
                vertOffset + n3 - 1;

            result.faceBrepIndices[triOffset + i - 1] = (int)brepIndex;
            result.faceSolidFaceIndices[triOffset + i - 1] = faceIndex;
        }

        vertOffset += nbNodes;
        triOffset += nbTris;
        faceIndex++;
    }

    // Discretize the B-rep's topological edges into polylines for the optional
    // edge/line display, and classify each as SHARP (feature) or TANGENT
    // (smooth, e.g. fillet<->face). The reconstructed shape uses a "fan"
    // topology (edges are not shared between faces), so we recover adjacency
    // geometrically: edges that coincide in 3D are the two sides of one
    // physical edge, and the angle between their owning faces' outward normals
    // tells sharp (angled) from tangent (parallel). De-dups the coincident
    // pair to one polyline. (Educated-guess classification — a producer-authored
    // edge-continuity flag would be the schema-level source of truth.)
    {
        // Discretize one edge into a polyline (Poly_Polygon3D if present, which
        // is coincident with the meshed faces; else sample the 3D curve).
        auto discretize = [&](const TopoDS_Edge& edge,
                              std::vector<GfVec3d>& pts) {
            TopLoc_Location eloc;
            Handle(Poly_Polygon3D) poly = BRep_Tool::Polygon3D(edge, eloc);
            if (!poly.IsNull()) {
                const gp_Trsf& et = eloc.Transformation();
                const TColgp_Array1OfPnt& nodes = poly->Nodes();
                pts.reserve(nodes.Length());
                for (int i = nodes.Lower(); i <= nodes.Upper(); ++i) {
                    gp_Pnt p = nodes.Value(i).Transformed(et);
                    pts.emplace_back(p.X(), p.Y(), p.Z());
                }
            } else {
                try {
                    BRepAdaptor_Curve curve(edge);
                    GCPnts_QuasiUniformDeflection disc(curve, deflection);
                    if (disc.IsDone()) {
                        pts.reserve(disc.NbPoints());
                        for (int i = 1; i <= disc.NbPoints(); ++i) {
                            gp_Pnt p = disc.Value(i);
                            pts.emplace_back(p.X(), p.Y(), p.Z());
                        }
                    }
                } catch (const Standard_Failure&) {
                }
            }
        };

        // Outward surface normal of \p face at the midpoint of \p edge's pcurve.
        auto faceNormalAtEdgeMid = [&](const TopoDS_Edge& edge,
                                       const TopoDS_Face& face,
                                       bool& ok) -> gp_Dir {
            ok = false;
            Standard_Real f2, l2;
            Handle(Geom2d_Curve) pc =
                BRep_Tool::CurveOnSurface(edge, face, f2, l2);
            TopLoc_Location loc;
            Handle(Geom_Surface) surf = BRep_Tool::Surface(face, loc);
            if (pc.IsNull() || surf.IsNull()) return gp_Dir(0, 0, 1);
            gp_Pnt2d uv = pc->Value(0.5 * (f2 + l2));
            gp_Pnt p; gp_Vec du, dv;
            surf->D1(uv.X(), uv.Y(), p, du, dv);
            gp_Vec n = du.Crossed(dv);
            if (n.Magnitude() < 1e-12) return gp_Dir(0, 0, 1);
            n.Normalize();
            if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
            if (!loc.IsIdentity()) n.Transform(loc.Transformation());
            ok = true;
            return gp_Dir(n);
        };

        // edge -> its owning face (1:1 in the fan topology).
        TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
        TopExp::MapShapesAndAncestors(
            shape, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

        struct EdgeRec {
            std::vector<GfVec3d> pts;
            gp_Dir normal;
            bool hasNormal = false;
        };
        std::vector<EdgeRec> recs;
        std::map<std::string, std::vector<size_t>> groups;  // geom-key -> recs

        auto keyOf = [](const std::vector<GfVec3d>& pts) {
            auto q = [](double v) {
                return (long long)std::llround(v * 1000.0);  // 1e-3 grid
            };
            const GfVec3d& a = pts.front();
            const GfVec3d& b = pts.back();
            const GfVec3d& m = pts[pts.size() / 2];
            long long ax=q(a[0]),ay=q(a[1]),az=q(a[2]);
            long long bx=q(b[0]),by=q(b[1]),bz=q(b[2]);
            // sort endpoints so a reversed coincident edge maps to the same key
            bool swap = std::tie(ax,ay,az) > std::tie(bx,by,bz);
            char buf[160];
            std::snprintf(buf, sizeof buf, "%lld,%lld,%lld|%lld,%lld,%lld|%lld,%lld,%lld",
                swap?bx:ax, swap?by:ay, swap?bz:az,
                swap?ax:bx, swap?ay:by, swap?az:bz,
                q(m[0]), q(m[1]), q(m[2]));
            return std::string(buf);
        };

        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
        for (int ei = 1; ei <= edgeMap.Extent(); ++ei) {
            const TopoDS_Edge& edge = TopoDS::Edge(edgeMap(ei));
            if (BRep_Tool::Degenerated(edge)) continue;
            EdgeRec rec;
            discretize(edge, rec.pts);
            if (rec.pts.size() < 2) continue;
            if (edgeFaceMap.Contains(edge)) {
                const TopTools_ListOfShape& faces = edgeFaceMap.FindFromKey(edge);
                if (!faces.IsEmpty()) {
                    rec.normal = faceNormalAtEdgeMid(
                        edge, TopoDS::Face(faces.First()), rec.hasNormal);
                }
            }
            size_t idx = recs.size();
            recs.push_back(std::move(rec));
            groups[keyOf(recs[idx].pts)].push_back(idx);
        }

        // Tangent if the two owning faces meet at a near-zero dihedral.
        const double tangentTol = 0.15;  // radians (~8.6 deg)
        for (const auto& kv : groups) {
            const std::vector<size_t>& ids = kv.second;
            const EdgeRec& a = recs[ids[0]];
            bool tangent = false;
            if (ids.size() >= 2) {
                const EdgeRec& b = recs[ids[1]];
                if (a.hasNormal && b.hasNormal) {
                    tangent = (a.normal.Angle(b.normal) < tangentTol);
                }
            }
            VtArray<int>& counts = tangent
                ? result.tangentEdgeCurveVertexCounts
                : result.edgeCurveVertexCounts;
            VtArray<GfVec3d>& points = tangent
                ? result.tangentEdgePoints : result.edgePoints;
            counts.push_back((int)a.pts.size());
            for (const auto& p : a.pts) points.push_back(p);
        }
    }

    result.success = true;
    return result;
}

/// Merge multiple per-Brep tessellation results into one.
///
/// Only successful results contribute geometry. Partial failure is now
/// reported rather than hidden: merged.success is true only when EVERY input
/// result succeeded, and each failed result's error is accumulated into
/// merged.errorMessage with its Brep index (debt register row 9 -- previously
/// success was hardcoded true and per-Brep errors were dropped). The
/// hasNormals/hasUVs flags are derived from the first SUCCESSFUL result, not
/// results[0]: if Brep 0 failed, results[0] is empty and reading its flags
/// dropped normals/UVs from every surviving Brep.
UsdSolidTessellationResult _MergeResults(
    const std::vector<UsdSolidTessellationResult>& results)
{
    UsdSolidTessellationResult merged;

    int totalVerts = 0;
    int totalFaces = 0;
    int failCount = 0;
    std::string errors;

    // First successful result: source of the hasNormals/hasUVs decision.
    const UsdSolidTessellationResult* firstOk = nullptr;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        if (!r.success) {
            ++failCount;
            if (!errors.empty()) errors += "; ";
            errors += TfStringPrintf(
                "Brep %zu: %s", i,
                r.errorMessage.empty() ? "tessellation failed"
                                       : r.errorMessage.c_str());
            continue;
        }
        if (!firstOk) firstOk = &r;
        totalVerts += (int)r.points.size();
        totalFaces += (int)r.faceVertexCounts.size();
    }

    // Success only if nothing failed; carry the accumulated errors either way.
    merged.success = (failCount == 0);
    merged.errorMessage = errors;
    if (failCount > 0) {
        TF_WARN("hdOcct: %d of %zu Breps failed to tessellate; merged mesh "
                "omits them. %s", failCount, results.size(), errors.c_str());
    }

    merged.points.reserve(totalVerts);
    merged.faceVertexCounts.reserve(totalFaces);
    merged.faceVertexIndices.reserve(totalFaces * 3);
    merged.faceBrepIndices.reserve(totalFaces);
    merged.faceSolidFaceIndices.reserve(totalFaces);

    const bool hasNormals = firstOk && !firstOk->normals.empty();
    const bool hasUVs = firstOk && !firstOk->uvs.empty();

    if (hasNormals) merged.normals.reserve(totalVerts);
    if (hasUVs) merged.uvs.reserve(totalVerts);

    int vertOffset = 0;
    for (const auto& r : results) {
        if (!r.success) continue;

        // Points
        for (const auto& p : r.points) merged.points.push_back(p);
        if (hasNormals) {
            for (const auto& n : r.normals) merged.normals.push_back(n);
        }
        if (hasUVs) {
            for (const auto& uv : r.uvs) merged.uvs.push_back(uv);
        }

        // Faces with offset indices
        for (const auto& fc : r.faceVertexCounts)
            merged.faceVertexCounts.push_back(fc);
        for (const auto& idx : r.faceVertexIndices)
            merged.faceVertexIndices.push_back(idx + vertOffset);
        for (const auto& bi : r.faceBrepIndices)
            merged.faceBrepIndices.push_back(bi);
        for (const auto& fi : r.faceSolidFaceIndices)
            merged.faceSolidFaceIndices.push_back(fi);

        // Edge polylines are a flat point array partitioned by per-curve
        // counts; no index remap needed, just concatenate both arrays.
        for (const auto& c : r.edgeCurveVertexCounts)
            merged.edgeCurveVertexCounts.push_back(c);
        for (const auto& p : r.edgePoints)
            merged.edgePoints.push_back(p);
        for (const auto& c : r.tangentEdgeCurveVertexCounts)
            merged.tangentEdgeCurveVertexCounts.push_back(c);
        for (const auto& p : r.tangentEdgePoints)
            merged.tangentEdgePoints.push_back(p);

        vertOffset += (int)r.points.size();
    }

    return merged;
}

} // anonymous namespace


std::vector<UsdSolidTessellationResult>
UsdSolidTessellator::Tessellate(
    const UsdPrim& brepArrayPrim,
    const UsdSolidTessellationParams& params) const
{
    UsdSolidBrepBuilder brepBuilder;
    std::vector<TopoDS_Shape> shapes = brepBuilder.Build(brepArrayPrim);

    if (shapes.empty()) {
        UsdSolidTessellationResult fail;
        fail.errorMessage = "Failed to build any shapes from BrepArray";
        return {fail};
    }

    std::vector<UsdSolidTessellationResult> results;
    results.reserve(shapes.size());

    for (size_t i = 0; i < shapes.size(); ++i) {
        results.push_back(_ExtractMesh(shapes[i], params, i));
    }

    if (params.mergeBreps) {
        return {_MergeResults(results)};
    }

    return results;
}

UsdSolidTessellationResult
UsdSolidTessellator::TessellateSingle(
    const UsdPrim& brepArrayPrim,
    size_t brepIndex,
    const UsdSolidTessellationParams& params) const
{
    UsdSolidBrepBuilder brepBuilder;
    auto shapeOpt = brepBuilder.BuildSingleBrep(brepArrayPrim, brepIndex);

    if (!shapeOpt.has_value()) {
        UsdSolidTessellationResult fail;
        fail.errorMessage = TfStringPrintf(
            "Failed to build Brep %zu from BrepArray '%s'",
            brepIndex, brepArrayPrim.GetPath().GetText());
        return fail;
    }

    return _ExtractMesh(*shapeOpt, params, brepIndex);
}

std::vector<SdfPath>
UsdSolidTessellator::TessellateToStage(
    const UsdPrim& brepArrayPrim,
    const SdfPath& destPath,
    const UsdSolidTessellationParams& params) const
{
    auto results = Tessellate(brepArrayPrim, params);

    UsdStageRefPtr stage = brepArrayPrim.GetStage();
    if (!stage) return {};

    UsdSolidMeshExporter exporter;
    return exporter.ExportAll(stage, destPath, results);
}

PXR_NAMESPACE_CLOSE_SCOPE

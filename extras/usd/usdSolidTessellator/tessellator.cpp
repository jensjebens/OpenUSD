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
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <ShapeFix_Shape.hxx>
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

    // Fix shape topology (adds missing pcurves required by BRepMesh)
    ShapeFix_Shape shapeFix(shape);
    shapeFix.Perform();
    const TopoDS_Shape& fixedShape = shapeFix.Shape();

    BRepMesh_IncrementalMesh mesher(
        fixedShape,
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
    for (TopExp_Explorer exp(fixedShape, TopAbs_FACE); exp.More(); exp.Next()) {
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

    for (TopExp_Explorer exp(fixedShape, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(exp.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) {
            faceIndex++;
            continue;
        }

        const gp_Trsf& trsf = loc.Transformation();
        bool isReversed = (face.Orientation() == TopAbs_REVERSED);
        // Check for mirrored transform (negative determinant).
        // Reference: StdPrs_ShadedShape.cxx uses (isReversed ^ isMirrored)
        // for normal flip. A mirrored Location flips the handedness.
        bool isMirrored = (trsf.IsNegative() != Standard_False);

        int nbNodes = tri->NbNodes();
        int nbTris = tri->NbTriangles();

        // Vertices
        for (int i = 1; i <= nbNodes; ++i) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            result.points[vertOffset + i - 1] = GfVec3d(p.X(), p.Y(), p.Z());
        }

        // Normals
        if (params.computeNormals) {
            // Prefer pre-computed normals from Poly_Triangulation (already
            // correctly oriented by BRepMesh). Fall back to du×dv + flip.
            if (tri->HasNormals()) {
                for (int i = 1; i <= nbNodes; ++i) {
                    gp_Dir nd = tri->Normal(i);
                    // Apply face orientation + mirror (same as StdPrs_ShadedShape)
                    if (isReversed != isMirrored) {
                        nd.Reverse();
                    }
                    if (!loc.IsIdentity()) {
                        nd.Transform(trsf);
                    }
                    result.normals[vertOffset + i - 1] =
                        GfVec3f(static_cast<float>(nd.X()),
                                static_cast<float>(nd.Y()),
                                static_cast<float>(nd.Z()));
                }
            } else if (tri->HasUVNodes()) {
            BRepAdaptor_Surface surfAdaptor(face, Standard_True);

            for (int i = 1; i <= nbNodes; ++i) {
                gp_Pnt2d uv = tri->UVNode(i);
                gp_Pnt pnt;
                gp_Vec du, dv;
                surfAdaptor.D1(uv.X(), uv.Y(), pnt, du, dv);
                gp_Vec normal = du.Crossed(dv);
                if (normal.Magnitude() > 1e-10) {
                    normal.Normalize();
                    // Use face topology orientation XOR mirror for flip.
                    // Reference: StdPrs_ShadedShape.cxx
                    if (isReversed != isMirrored) {
                        normal.Reverse();
                    }
                    // Transform normal by location (rotation only)
                    if (!loc.IsIdentity()) {
                        normal.Transform(trsf);
                    }
                    result.normals[vertOffset + i - 1] =
                        GfVec3f(static_cast<float>(normal.X()),
                                static_cast<float>(normal.Y()),
                                static_cast<float>(normal.Z()));
                } else {
                    result.normals[vertOffset + i - 1] = GfVec3f(0, 0, 1);
                }
            }
            } // end else if (tri->HasUVNodes())
        } // end if (params.computeNormals)

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

            // Winding swap: OCCT tessellation winding convention.
            // For REVERSED faces, swap indices to maintain consistent
            // winding relative to the computed outward normal.
            // For FORWARD faces, keep as-is (base winding matches du×dv).
            // Reference: StdPrs_ShadedShape.cxx, IVtkOCC_ShapeMesher.cxx
            if (isReversed) {
                std::swap(n2, n3);
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

    result.success = true;
    return result;
}

/// Merge multiple tessellation results into one.
UsdSolidTessellationResult _MergeResults(
    const std::vector<UsdSolidTessellationResult>& results)
{
    UsdSolidTessellationResult merged;
    merged.success = true;

    int totalVerts = 0;
    int totalFaces = 0;

    for (const auto& r : results) {
        if (!r.success) continue;
        totalVerts += (int)r.points.size();
        totalFaces += (int)r.faceVertexCounts.size();
    }

    merged.points.reserve(totalVerts);
    merged.faceVertexCounts.reserve(totalFaces);
    merged.faceVertexIndices.reserve(totalFaces * 3);
    merged.faceBrepIndices.reserve(totalFaces);
    merged.faceSolidFaceIndices.reserve(totalFaces);

    // Check if ANY result has normals/UVs (not just the first one)
    bool hasNormals = false;
    bool hasUVs = false;
    for (const auto& r : results) {
        if (!r.normals.empty()) hasNormals = true;
        if (!r.uvs.empty()) hasUVs = true;
        if (hasNormals && hasUVs) break;
    }

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

    // Compact before export
    for (auto& r : results) {
        if (r.success) r.Compact();
    }

    UsdStageRefPtr stage = brepArrayPrim.GetStage();
    if (!stage) return {};

    UsdSolidMeshExporter exporter;
    return exporter.ExportAll(stage, destPath, results);
}

// ---------------------------------------------------------------------------
// UsdSolidTessellationResult::Compact
// ---------------------------------------------------------------------------

void
UsdSolidTessellationResult::Compact()
{
    if (points.empty() || faceVertexIndices.empty()) return;

    const size_t numPoints = points.size();
    std::vector<int> oldToNew(numPoints, -1);
    int newIdx = 0;

    // Build remapping: only vertices referenced by indices survive
    for (int idx : faceVertexIndices) {
        if (idx >= 0 && static_cast<size_t>(idx) < numPoints
            && oldToNew[idx] == -1) {
            oldToNew[idx] = newIdx++;
        }
    }

    // Early out if already compact
    if (static_cast<size_t>(newIdx) == numPoints) return;

    // Compact points
    VtArray<GfVec3d> newPoints(newIdx);
    for (size_t i = 0; i < numPoints; ++i) {
        if (oldToNew[i] >= 0) {
            newPoints[oldToNew[i]] = points[i];
        }
    }
    points = std::move(newPoints);

    // Remap indices
    for (int& idx : faceVertexIndices) {
        idx = (idx >= 0 && static_cast<size_t>(idx) < numPoints)
              ? oldToNew[idx] : 0;
    }

    // Compact normals (vertex interpolation)
    if (!normals.empty()) {
        VtArray<GfVec3f> newNormals(newIdx);
        for (size_t i = 0; i < std::min(normals.size(), numPoints); ++i) {
            if (oldToNew[i] >= 0) {
                newNormals[oldToNew[i]] = normals[i];
            }
        }
        normals = std::move(newNormals);
    }

    // Compact UVs (vertex interpolation)
    if (!uvs.empty()) {
        VtArray<GfVec2f> newUvs(newIdx);
        for (size_t i = 0; i < std::min(uvs.size(), numPoints); ++i) {
            if (oldToNew[i] >= 0) {
                newUvs[oldToNew[i]] = uvs[i];
            }
        }
        uvs = std::move(newUvs);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
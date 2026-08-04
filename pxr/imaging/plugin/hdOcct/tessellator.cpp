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
/// \p authoredFaceIdx maps compound face position -> authored face index
/// (the builder can drop faces, shifting positions); null = identity.
UsdSolidTessellationResult _ExtractMesh(
    const TopoDS_Shape& shape,
    const UsdSolidTessellationParams& params,
    size_t brepIndex,
    const std::vector<int>* authoredFaceIdx = nullptr)
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
            result.faceSolidFaceIndices[triOffset + i - 1] =
                (authoredFaceIdx && (size_t)faceIndex < authoredFaceIdx->size())
                    ? (*authoredFaceIdx)[faceIndex]
                    : faceIndex;
        }

        vertOffset += nbNodes;
        triOffset += nbTris;
        faceIndex++;
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

    const auto& authoredMaps = brepBuilder.GetBuiltFaceAuthoredIndices();
    for (size_t i = 0; i < shapes.size(); ++i) {
        const std::vector<int>* faceMap =
            (i < authoredMaps.size() && !authoredMaps[i].empty())
                ? &authoredMaps[i]
                : nullptr;
        results.push_back(_ExtractMesh(shapes[i], params, i, faceMap));
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

    const auto& authoredMaps = brepBuilder.GetBuiltFaceAuthoredIndices();
    const std::vector<int>* faceMap =
        (!authoredMaps.empty() && !authoredMaps[0].empty())
            ? &authoredMaps[0]
            : nullptr;
    return _ExtractMesh(*shapeOpt, params, brepIndex, faceMap);
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

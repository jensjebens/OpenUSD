// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Hydra Generative Procedural Plugin for UsdSolid BrepArray tessellation.
//
// Registers as an HdGpGenerativeProceduralPlugin so that BrepArray prims
// are automatically tessellated into mesh geometry visible in any Hydra
// renderer (Storm, HdPrman, etc.) without requiring explicit conversion.

#ifndef USD_SOLID_TESSELLATOR_HYDRA_GENERATIVE_PLUGIN_H
#define USD_SOLID_TESSELLATOR_HYDRA_GENERATIVE_PLUGIN_H

#include "pxr/pxr.h"
#include "api.h"
#include "pxr/imaging/hdGp/generativeProcedural.h"
#include "pxr/imaging/hdGp/generativeProceduralPlugin.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

// --------------------------------------------------------------------------
// HdGpGenerativeProcedural subclass: BrepArray tessellation
// --------------------------------------------------------------------------

/// \class UsdSolidTessellationProcedural
///
/// A Hydra generative procedural that tessellates UsdSolid BrepArray prims
/// into child mesh prims on the fly. When a BrepArray prim is encountered
/// during Hydra scene index traversal, this procedural:
///
///   1. Reads the BrepArray topology and NURBS geometry from the USD stage
///   2. Builds OCCT TopoDS_Shape via UsdSolidBrepBuilder
///   3. Tessellates via BRepMesh_IncrementalMesh
///   4. Emits child mesh prim(s) into the Hydra scene index
///
/// Tessellation parameters can be controlled via primvars on the BrepArray:
///   - primvars:tessellation:linearDeflection (double)
///   - primvars:tessellation:angularDeflection (double)
///   - primvars:tessellation:computeNormals (bool)
///
class USDSOLIDTESSELLATOR_API UsdSolidTessellationProcedural
    : public HdGpGenerativeProcedural
{
public:
    UsdSolidTessellationProcedural(const SdfPath &proceduralPrimPath);
    ~UsdSolidTessellationProcedural() override;

    // Returns dependencies — we depend on our own prim's data sources
    // (the BrepArray attributes).
    DependencyMap UpdateDependencies(
        const HdSceneIndexBaseRefPtr &inputScene) override;

    // Primary cook: tessellate and return child mesh prim paths/types.
    ChildPrimTypeMap Update(
        const HdSceneIndexBaseRefPtr &inputScene,
        const ChildPrimTypeMap &previousResult,
        const DependencyMap &dirtiedDependencies,
        HdSceneIndexObserver::DirtiedPrimEntries *outputDirtiedPrims) override;

    // Return data source for a child prim (the tessellated mesh data).
    HdSceneIndexPrim GetChildPrim(
        const HdSceneIndexBaseRefPtr &inputScene,
        const SdfPath &childPrimPath) override;

private:
    struct _MeshData {
        VtArray<GfVec3f> points;
        VtArray<int> faceVertexCounts;
        VtArray<int> faceVertexIndices;
        VtArray<GfVec3f> normals;
    };

    // B-rep edge polylines emitted as a linear HdBasisCurves child prim,
    // toggled via purpose=guide so it behaves like a wireframe overlay.
    struct _EdgeData {
        VtArray<GfVec3f> points;
        VtArray<int> curveVertexCounts;
    };

    std::vector<_MeshData> _meshes;
    std::vector<_EdgeData> _edges;
    bool _cooked = false;
    std::mutex _mutex;  // OCCT is not thread-safe

    void _Tessellate(const HdSceneIndexBaseRefPtr &inputScene);
};

// --------------------------------------------------------------------------
// Plugin class
// --------------------------------------------------------------------------

/// \class UsdSolidTessellationProceduralPlugin
///
/// Plugin registration for the tessellation procedural. Discovered via
/// plugInfo.json with type "UsdSolidTessellationProceduralPlugin".
///
class USDSOLIDTESSELLATOR_API UsdSolidTessellationProceduralPlugin
    : public HdGpGenerativeProceduralPlugin
{
public:
    UsdSolidTessellationProceduralPlugin();
    ~UsdSolidTessellationProceduralPlugin() override;

    HdGpGenerativeProcedural *Construct(
        const SdfPath &proceduralPrimPath) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_SOLID_TESSELLATOR_HYDRA_GENERATIVE_PLUGIN_H

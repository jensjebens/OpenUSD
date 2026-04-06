//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_LOD_SCENE_INDEX_H
#define PXR_IMAGING_HD_LOD_SCENE_INDEX_H

/// \file hdLod/hdLodSceneIndex.h

#include "pxr/pxr.h"
#include "pxr/imaging/hdLod/api.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/vt/array.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(HdLodSceneIndex);

/// \class HdLodSceneIndex
///
/// A scene index filter that implements LOD (Level of Detail) switching.
///
/// This filter reads LodGroupAPI and LodItemAPI from the USD stage
/// (via a captured stage reference) and evaluates distance-based LOD
/// selection with hysteresis. Non-active LodItem prims have visibility=false
/// overlaid on all their renderable descendants.
///
/// The stage is captured via UsdNotice::StageContentsChanged, following
/// the same pattern as HdExecComputedTransformSceneIndex.
///
class HdLodSceneIndex final : public HdSingleInputFilteringSceneIndexBase
{
public:
    HDLOD_API
    static HdLodSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex);

    /// Set the USD stage used to read LOD schema data.
    HDLOD_API
    static void SetGlobalStage(const UsdStageRefPtr &stage);

    // HdSceneIndexBase overrides
    HDLOD_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    HDLOD_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HDLOD_API
    HdLodSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);

    // HdSingleInputFilteringSceneIndexBase overrides
    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    // Lazy bootstrap: capture stage on first access if available.
    void _TryBootstrap();

    // Evaluate LOD selection for all LodGroup prims and update
    // _hiddenRenderables. Sends dirty notices for any changed renderables.
    void _EvaluateLod();

    // Rebuild the _lodGroups and _descendantCache maps by walking the scene.
    void _RebuildCache();

    // Walk descendants of lodItemPath, collecting renderable paths into out.
    void _CollectRenderables(const SdfPath &primPath,
                             std::vector<SdfPath> &out) const;

    // Get camera world position from the active camera prim's xform.
    GfVec3d _GetCameraPosition() const;

    // Check if a prim type is renderable (Rprim).
    static bool _IsRenderable(const TfToken &primType);

    // For each LodGroup prim path, the ordered list of LodItem prim paths.
    std::unordered_map<SdfPath, std::vector<SdfPath>, SdfPath::Hash> _lodGroups;

    // For each LodItem prim path, the list of renderable descendant paths.
    std::unordered_map<SdfPath, std::vector<SdfPath>, SdfPath::Hash>
        _descendantCache;

    // Set of renderable paths that are currently hidden due to LOD.
    std::unordered_set<SdfPath, SdfPath::Hash> _hiddenRenderables;

    // Cached camera path (from render settings active camera).
    mutable SdfPath _cameraPath;

    // Per-group previous active LOD index for hysteresis.
    std::unordered_map<SdfPath, int, SdfPath::Hash> _prevActiveIndex;

    // Per-group cached thresholds (read from LodGroup prim).
    struct _GroupThresholds {
        VtArray<float> minThresholds;
        VtArray<float> maxThresholds;
    };
    std::unordered_map<SdfPath, _GroupThresholds, SdfPath::Hash>
        _groupThresholds;

    // Reentrancy guard to prevent infinite rebuild loops
    bool _evaluating = false;

    // USD stage reference for reading LOD schema data (relationships, attrs)
    UsdStageRefPtr _stage;

    // Global stage captured via UsdNotice
    static UsdStageRefPtr _sGlobalStage;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_LOD_SCENE_INDEX_H

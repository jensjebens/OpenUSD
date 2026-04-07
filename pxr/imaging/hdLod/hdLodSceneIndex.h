//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_LOD_SCENE_INDEX_H
#define PXR_IMAGING_HD_LOD_SCENE_INDEX_H

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

class HdLodSceneIndex final : public HdSingleInputFilteringSceneIndexBase
{
public:
    HDLOD_API
    static HdLodSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HDLOD_API
    static void SetGlobalStage(const UsdStageRefPtr &stage);

    HDLOD_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    HDLOD_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HDLOD_API
    HdLodSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);

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
    // Cache rebuild (safe in _PrimsAdded — no xform reads)
    void _RebuildGroupCache();
    void _CollectRenderables(const SdfPath &primPath,
                             std::vector<SdfPath> &out) const;
    static bool _IsRenderable(const TfToken &primType);

    // Camera position update (safe in _PrimsDirtied)
    void _UpdateCameraPosition();

    // LOD evaluation (safe in _PrimsDirtied, after camera update)
    void _EvaluateLod();

    // LOD group data (from USD stage)
    struct _GroupData {
        std::vector<SdfPath> lodItems;
        VtArray<float> minThresholds;
        VtArray<float> maxThresholds;
    };
    std::unordered_map<SdfPath, _GroupData, SdfPath::Hash> _lodGroups;

    // Renderable descendants per LOD item
    std::unordered_map<SdfPath, std::vector<SdfPath>, SdfPath::Hash>
        _descendantCache;

    // Hidden renderables (visibility overlay)
    std::unordered_set<SdfPath, SdfPath::Hash> _hiddenRenderables;

    // Per-group hysteresis state
    std::unordered_map<SdfPath, int, SdfPath::Hash> _prevActiveIndex;

    // Cached camera state
    SdfPath _cameraPath;
    GfVec3d _cachedCameraPos{0, 0, 0};
    bool _evaluatingLod = false;
    bool _lodInitialized = false;

    // Stage reference
    UsdStageRefPtr _stage;
    static UsdStageRefPtr _sGlobalStage;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

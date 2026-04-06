//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdLod/hdLodSceneIndex.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/weakBase.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdUtils/stageCache.h"

#include <algorithm>
#include <cstdlib>

PXR_NAMESPACE_OPEN_SCOPE

// Static member
UsdStageRefPtr HdLodSceneIndex::_sGlobalStage;

// Global listener that captures stages (same pattern as HdExec)
namespace {
class _StageListener : public TfWeakBase {
public:
    void Register() {
        TfNotice::Register(
            TfCreateWeakPtr(this),
            &_StageListener::_OnStageContentsChanged);
    }
    void _OnStageContentsChanged(
        const UsdNotice::StageContentsChanged &notice) {
        UsdStageWeakPtr sender = notice.GetStage();
        if (sender) {
            HdLodSceneIndex::SetGlobalStage(UsdStageRefPtr(sender));
        }
    }
};
static _StageListener *_sListener = nullptr;
} // anonymous namespace

TF_REGISTRY_FUNCTION(HdLodSceneIndex)
{
    static _StageListener listener;
    listener.Register();
    _sListener = &listener;
}

/* static */
void
HdLodSceneIndex::SetGlobalStage(const UsdStageRefPtr &stage)
{
    _sGlobalStage = stage;
}

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // LodGroupAPI relationship name
    (lodItems)

    // LodDistanceHeuristicAPI attribute base names
    // Attributes follow the pattern:
    //   lod:Heuristic:<domain>:distance:minThresholds
    //   lod:Heuristic:<domain>:distance:maxThresholds
    ((lodHeuristicPrefix, "lod:Heuristic"))
    ((distanceMinSuffix, "distance:minThresholds"))
    ((distanceMaxSuffix, "distance:maxThresholds"))

    // apiSchemas token for reading schema metadata
    (apiSchemas)

    // Schema names
    ((lodGroupAPI, "LodGroupAPI"))
    ((lodItemAPI,  "LodItemAPI"))
    ((lodDistanceHeuristicAPI, "LodDistanceHeuristicAPI"))

    // Render settings active camera
    (renderSettings)
    (activeCamera)
    ((camera, "camera"))
);

// ---------------------------------------------------------------------------
// _InvisibleDataSource
//
// Wraps an existing prim data source and overrides the visibility schema to
// return false.
// ---------------------------------------------------------------------------
namespace {

class _InvisibleDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_InvisibleDataSource);

    _InvisibleDataSource(HdContainerDataSourceHandle const &input)
        : _input(input)
    {
        // Build the invisible visibility container using the Builder.
        _visibilityDs = HdVisibilitySchema::Builder()
            .SetVisibility(
                HdRetainedTypedSampledDataSource<bool>::New(false))
            .Build();
    }

    TfTokenVector GetNames() override
    {
        if (!_input) {
            return { HdVisibilitySchemaTokens->visibility };
        }
        TfTokenVector names = _input->GetNames();
        const TfToken &visToken = HdVisibilitySchemaTokens->visibility;
        if (std::find(names.begin(), names.end(), visToken) == names.end()) {
            names.push_back(visToken);
        }
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdVisibilitySchemaTokens->visibility) {
            return _visibilityDs;
        }
        if (_input) {
            return _input->Get(name);
        }
        return nullptr;
    }

private:
    HdContainerDataSourceHandle _input;
    HdContainerDataSourceHandle _visibilityDs;
};

HD_DECLARE_DATASOURCE_HANDLES(_InvisibleDataSource);

} // anonymous namespace

// ---------------------------------------------------------------------------
// HdLodSceneIndex
// ---------------------------------------------------------------------------

/* static */
HdLodSceneIndexRefPtr
HdLodSceneIndex::New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(new HdLodSceneIndex(inputSceneIndex));
}

HdLodSceneIndex::HdLodSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    , _stage(_sGlobalStage)
{
    if (_stage) {
        TF_STATUS("hdLod: stage captured (%zu prims)",
            (size_t)std::distance(
                _stage->Traverse().begin(), _stage->Traverse().end()));
        _RebuildCache();
        _EvaluateLod();
    } else {
        TF_STATUS("hdLod: no stage available yet (will rebuild on PrimsAdded)");
    }
}

HdSceneIndexPrim
HdLodSceneIndex::GetPrim(const SdfPath &primPath) const
{
    const_cast<HdLodSceneIndex*>(this)->_TryBootstrap();

    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (_hiddenRenderables.count(primPath)) {
        prim.dataSource =
            _InvisibleDataSource::New(prim.dataSource);
    }
    return prim;
}

SdfPathVector
HdLodSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdLodSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    _TryBootstrap();

    if (_stage && !_evaluating) {
        _RebuildCache();
        _EvaluateLod();
    }
    _SendPrimsAdded(entries);
}

void
HdLodSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    // Clean up any removed paths from cache structures.
    for (const auto &entry : entries) {
        const SdfPath &path = entry.primPath;
        _lodGroups.erase(path);
        _descendantCache.erase(path);
        _hiddenRenderables.erase(path);
    }
    _RebuildCache();
    _EvaluateLod();
    _SendPrimsRemoved(entries);
}

void
HdLodSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    // Re-evaluate LOD if xform or relevant attributes may have changed.
    bool needsReevaluation = false;
    for (const auto &entry : entries) {
        // Re-evaluate if any xform is dirtied (camera may have moved)
        // or if a lod attribute is dirtied.
        for (const auto &locator : entry.dirtyLocators) {
            const TfToken &first = locator.IsEmpty()
                ? TfToken()
                : locator.GetFirstElement();
            if (first == HdXformSchemaTokens->xform ||
                first == TfToken("primvars") ||
                first == TfToken("lod")) {
                needsReevaluation = true;
                break;
            }
        }
        if (needsReevaluation) {
            break;
        }
    }

    if (needsReevaluation && !_evaluating) {
        _EvaluateLod();
    }

    _SendPrimsDirtied(entries);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

bool
HdLodSceneIndex::_IsRenderable(const TfToken &primType)
{
    // HdPrimTypeIsGprim covers mesh, basisCurves, points, volume.
    // We also need implicit geometry types (sphere, cube, cone, etc.)
    if (HdPrimTypeIsGprim(primType)) {
        return true;
    }
    // Check implicit geometry types
    static const TfTokenVector implicitTypes = {
        TfToken("sphere"),
        TfToken("cube"),
        TfToken("cone"),
        TfToken("cylinder"),
        TfToken("capsule"),
        TfToken("plane"),
        TfToken("tetMesh"),
    };
    for (const TfToken &t : implicitTypes) {
        if (primType == t) {
            return true;
        }
    }
    return false;
}

void
HdLodSceneIndex::_CollectRenderables(
    const SdfPath &primPath,
    std::vector<SdfPath> &out) const
{
    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();
    HdSceneIndexPrim prim = input->GetPrim(primPath);
    if (_IsRenderable(prim.primType)) {
        out.push_back(primPath);
    }
    for (const SdfPath &child : input->GetChildPrimPaths(primPath)) {
        _CollectRenderables(child, out);
    }
}









void
HdLodSceneIndex::_RebuildCache()
{
    _lodGroups.clear();
    _descendantCache.clear();
    _groupThresholds.clear();

    if (!_stage) {
        return;
    }

    // Walk the USD stage to find LodGroup prims by checking for
    // the lod:lodItems relationship (works for unregistered schemas).
    for (UsdPrim prim : _stage->Traverse()) {
        UsdRelationship lodItemsRel =
            prim.GetRelationship(TfToken("lod:lodItems"));
        if (!lodItemsRel || !lodItemsRel.HasAuthoredTargets()) {
            continue;
        }

        SdfPathVector targets;
        lodItemsRel.GetTargets(&targets);
        if (targets.empty()) {
            continue;
        }

        SdfPath groupPath = prim.GetPath();
        _lodGroups[groupPath] = targets;

        TF_STATUS("hdLod: found LodGroup %s with %zu items",
            groupPath.GetText(), targets.size());

        // Read distance thresholds from the group prim
        _GroupThresholds thresholds;
        for (UsdAttribute attr : prim.GetAttributes()) {
            const std::string &name = attr.GetName().GetString();
            if (TfStringStartsWith(name, "lod:Heuristic:") &&
                TfStringEndsWith(name, ":distance:minThresholds")) {
                VtArray<float> val;
                if (attr.Get(&val)) {
                    thresholds.minThresholds = val;
                }
            } else if (TfStringStartsWith(name, "lod:Heuristic:") &&
                       TfStringEndsWith(name, ":distance:maxThresholds")) {
                VtArray<float> val;
                if (attr.Get(&val)) {
                    thresholds.maxThresholds = val;
                }
            }
        }
        if (thresholds.maxThresholds.empty() &&
            !thresholds.minThresholds.empty()) {
            thresholds.maxThresholds = thresholds.minThresholds;
        }
        if (!thresholds.minThresholds.empty()) {
            TF_STATUS("hdLod: group %s thresholds min=[%s] max=[%s]",
                groupPath.GetText(),
                TfStringify(thresholds.minThresholds).c_str(),
                TfStringify(thresholds.maxThresholds).c_str());
        }
        _groupThresholds[groupPath] = std::move(thresholds);
    }

    // Collect renderable descendants from the Hydra scene index.
    for (const auto &groupEntry : _lodGroups) {
        for (const SdfPath &itemPath : groupEntry.second) {
            if (true) {  // Always re-collect (cache cleared above)
                std::vector<SdfPath> renderables;
                // Check what the scene index says about this prim
                const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();
                HdSceneIndexPrim itemPrim = input->GetPrim(itemPath);
                TF_STATUS("hdLod: GetPrim(%s) type=%s ds=%s children=%zu",
                    itemPath.GetText(),
                    itemPrim.primType.GetText(),
                    itemPrim.dataSource ? "yes" : "no",
                    input->GetChildPrimPaths(itemPath).size());
                _CollectRenderables(itemPath, renderables);
                _descendantCache[itemPath] = std::move(renderables);
                TF_STATUS("hdLod: item %s has %zu renderables",
                    itemPath.GetText(),
                    _descendantCache[itemPath].size());
            }
        }
    }
}

void
HdLodSceneIndex::_TryBootstrap()
{
    if (_stage) {
        return;  // Already bootstrapped
    }
    // Try global stage from notice listener
    if (_sGlobalStage) {
        _stage = _sGlobalStage;
        TF_STATUS("hdLod: stage captured via notice listener");
        _RebuildCache();
        _EvaluateLod();
        return;
    }
    // Fallback: try UsdUtilsStageCache
    UsdStageCache cache = UsdUtilsStageCache::Get();
    auto allStages = cache.GetAllStages();
    if (!allStages.empty()) {
        _stage = allStages.front();
        TF_STATUS("hdLod: stage captured from UsdUtilsStageCache");
        _RebuildCache();
        _EvaluateLod();
        return;
    }
    // Fallback: open stage from HDLOD_STAGE_PATH env var
    const char* stagePath = std::getenv("HDLOD_STAGE_PATH");
    if (stagePath && stagePath[0]) {
        _stage = UsdStage::Open(stagePath);
        if (_stage) {
            TF_STATUS("hdLod: stage opened from HDLOD_STAGE_PATH=%s",
                stagePath);
            _RebuildCache();
            _EvaluateLod();
        }
    }
}

GfVec3d
HdLodSceneIndex::_GetCameraPosition() const
{
    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();

    // Try to find camera path from render settings if not already cached.
    if (_cameraPath.IsEmpty()) {
        // Look for a renderSettings prim under /Render or root.
        SdfPathVector rsChildren =
            input->GetChildPrimPaths(SdfPath::AbsoluteRootPath());
        for (const SdfPath &child : rsChildren) {
            HdSceneIndexPrim prim = input->GetPrim(child);
            if (prim.primType == TfToken("renderSettings")) {
                if (prim.dataSource) {
                    HdDataSourceBaseHandle camDs =
                        prim.dataSource->Get(_tokens->activeCamera);
                    if (!camDs) {
                        camDs = prim.dataSource->Get(_tokens->camera);
                    }
                    if (camDs) {
                        using PathDs = HdTypedSampledDataSource<SdfPath>;
                        if (PathDs::Handle pd = PathDs::Cast(camDs)) {
                            _cameraPath = pd->GetTypedValue(0.0f);
                        }
                    }
                }
                break;
            }
        }
    }

    if (!_cameraPath.IsEmpty()) {
        HdSceneIndexPrim camPrim = input->GetPrim(_cameraPath);
        if (camPrim.dataSource) {
            HdXformSchema xformSchema =
                HdXformSchema::GetFromParent(camPrim.dataSource);
            if (HdMatrixDataSourceHandle matDs = xformSchema.GetMatrix()) {
                GfMatrix4d mat = matDs->GetTypedValue(0.0f);
                return mat.ExtractTranslation();
            }
        }
    }

    return GfVec3d(0.0, 0.0, 0.0);
}

void
HdLodSceneIndex::_EvaluateLod()
{
    if (_evaluating) return;
    _evaluating = true;

    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();
    GfVec3d cameraPos = _GetCameraPosition();

    std::unordered_set<SdfPath, SdfPath::Hash> newHidden;

    // Track which LOD groups are inside inactive items (for hierarchical eval)
    std::unordered_set<SdfPath, SdfPath::Hash> inactiveSubtrees;

    for (const auto &groupEntry : _lodGroups) {
        const SdfPath &groupPath = groupEntry.first;
        const std::vector<SdfPath> &lodItems = groupEntry.second;

        if (lodItems.empty()) {
            continue;
        }

        // Axiom 1: skip if inside an inactive subtree (hierarchical gating)
        bool skip = false;
        for (const SdfPath &inactive : inactiveSubtrees) {
            if (groupPath.HasPrefix(inactive)) {
                skip = true;
                break;
            }
        }
        if (skip) {
            // Hide ALL renderables in all items of this skipped group
            for (const SdfPath &itemPath : lodItems) {
                auto cacheIt = _descendantCache.find(itemPath);
                if (cacheIt != _descendantCache.end()) {
                    for (const SdfPath &rPath : cacheIt->second) {
                        newHidden.insert(rPath);
                    }
                }
            }
            continue;
        }

        // Get the world position of the group prim for distance computation.
        GfVec3d groupPos(0.0, 0.0, 0.0);
        {
            HdSceneIndexPrim groupPrim = input->GetPrim(groupPath);
            if (groupPrim.dataSource) {
                HdXformSchema xformSchema =
                    HdXformSchema::GetFromParent(groupPrim.dataSource);
                if (HdMatrixDataSourceHandle matDs = xformSchema.GetMatrix()) {
                    groupPos =
                        matDs->GetTypedValue(0.0f).ExtractTranslation();
                }
            }
        }

        double distance = (cameraPos - groupPos).GetLength();

        // Read thresholds from cached group data
        auto threshIt = _groupThresholds.find(groupPath);
        VtArray<float> minThresholds, maxThresholds;
        if (threshIt != _groupThresholds.end()) {
            minThresholds = threshIt->second.minThresholds;
            maxThresholds = threshIt->second.maxThresholds;
        }

        int nThresholds = static_cast<int>(minThresholds.size());
        int nItems = static_cast<int>(lodItems.size());
        int activeIndex = 0;

        if (nThresholds > 0) {
            // Get previous active index for hysteresis
            auto prevIt = _prevActiveIndex.find(groupPath);
            bool hasPrev = (prevIt != _prevActiveIndex.end());
            int prevIndex = hasPrev ? prevIt->second : -1;

            if (!hasPrev) {
                // No previous state — use min thresholds (standard evaluation)
                activeIndex = nItems - 1;  // default: lowest detail
                for (int i = 0; i < nThresholds; ++i) {
                    if (distance < static_cast<double>(minThresholds[i])) {
                        activeIndex = i;
                        break;
                    }
                }
            } else {
                // Hysteresis evaluation:
                // - To go UP (higher index = less detail): use max thresholds
                // - To go DOWN (lower index = more detail): use min thresholds
                // - In between: stay at previous

                // What index would max thresholds give? (for going UP)
                int maxIndex = nItems - 1;
                for (int i = 0; i < nThresholds && i < (int)maxThresholds.size(); ++i) {
                    if (distance < static_cast<double>(maxThresholds[i])) {
                        maxIndex = i;
                        break;
                    }
                }

                // What index would min thresholds give? (for going DOWN)
                int minIndex = nItems - 1;
                for (int i = 0; i < nThresholds; ++i) {
                    if (distance < static_cast<double>(minThresholds[i])) {
                        minIndex = i;
                        break;
                    }
                }

                if (maxIndex > prevIndex) {
                    // Max thresholds say go higher → go higher
                    activeIndex = maxIndex;
                } else if (minIndex < prevIndex) {
                    // Min thresholds say go lower → go lower
                    activeIndex = minIndex;
                } else {
                    // In hysteresis dead zone → stay at previous
                    activeIndex = prevIndex;
                }
            }
        }

        // Clamp to valid range
        activeIndex = std::max(0, std::min(activeIndex, nItems - 1));

        // Store active index for next frame's hysteresis
        _prevActiveIndex[groupPath] = activeIndex;

        // Mark non-active item subtrees as inactive (Axiom 1: hierarchical)
        for (int i = 0; i < nItems; ++i) {
            if (i != activeIndex) {
                inactiveSubtrees.insert(lodItems[i]);
            }
        }

        // Hide all non-active items' renderables.
        for (int i = 0; i < nItems; ++i) {
            if (i == activeIndex) {
                continue;
            }
            const SdfPath &itemPath = lodItems[i];
            auto cacheIt = _descendantCache.find(itemPath);
            if (cacheIt != _descendantCache.end()) {
                for (const SdfPath &rPath : cacheIt->second) {
                    newHidden.insert(rPath);
                }
            }
        }
    }

    // Determine what changed and send dirty notices.
    HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;
    static const HdDataSourceLocatorSet visLocators{
        HdVisibilitySchema::GetDefaultLocator()};

    // Prims that were hidden and are now visible (or vice versa).
    for (const SdfPath &path : _hiddenRenderables) {
        if (!newHidden.count(path)) {
            dirtyEntries.push_back({path, visLocators});
        }
    }
    for (const SdfPath &path : newHidden) {
        if (!_hiddenRenderables.count(path)) {
            dirtyEntries.push_back({path, visLocators});
        }
    }

    _hiddenRenderables = std::move(newHidden);

    _evaluating = false;

    if (!dirtyEntries.empty()) {
        _SendPrimsDirtied(dirtyEntries);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

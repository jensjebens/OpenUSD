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
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/weakBase.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/xformable.h"

#include <algorithm>
#include <cstdlib>

PXR_NAMESPACE_OPEN_SCOPE

UsdStageRefPtr HdLodSceneIndex::_sGlobalStage;

// Stage notice listener
namespace {
class _StageListener : public TfWeakBase {
public:
    void Register() {
        TfNotice::Register(TfCreateWeakPtr(this),
            &_StageListener::_OnChange);
    }
    void _OnChange(const UsdNotice::StageContentsChanged &n) {
        if (auto s = n.GetStage())
            HdLodSceneIndex::SetGlobalStage(UsdStageRefPtr(s));
    }
};
} // anon

TF_REGISTRY_FUNCTION(HdLodSceneIndex) {
    static _StageListener l; l.Register();
}

/* static */
void HdLodSceneIndex::SetGlobalStage(const UsdStageRefPtr &s) {
    _sGlobalStage = s;
}

// ---------------------------------------------------------------------------
// Thread-safe invisible visibility data source
// ---------------------------------------------------------------------------
namespace {

class _InvisibleDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_InvisibleDataSource);

    _InvisibleDataSource(HdContainerDataSourceHandle const &input)
        : _input(input) {}

    TfTokenVector GetNames() override {
        if (!_input) return {HdVisibilitySchemaTokens->visibility};
        TfTokenVector n = _input->GetNames();
        if (std::find(n.begin(), n.end(),
                HdVisibilitySchemaTokens->visibility) == n.end())
            n.push_back(HdVisibilitySchemaTokens->visibility);
        return n;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override {
        if (name == HdVisibilitySchemaTokens->visibility) {
            static HdContainerDataSourceHandle s =
                HdRetainedContainerDataSource::New(
                    HdVisibilitySchemaTokens->visibility,
                    HdRetainedTypedSampledDataSource<bool>::New(false));
            return s;
        }
        return _input ? _input->Get(name) : nullptr;
    }

private:
    HdContainerDataSourceHandle _input;
};
HD_DECLARE_DATASOURCE_HANDLES(_InvisibleDataSource);

} // anon

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HdLodSceneIndexRefPtr
HdLodSceneIndex::New(const HdSceneIndexBaseRefPtr &in) {
    return TfCreateRefPtr(new HdLodSceneIndex(in));
}

HdLodSceneIndex::HdLodSceneIndex(const HdSceneIndexBaseRefPtr &in)
    : HdSingleInputFilteringSceneIndexBase(in)
{
    // Try to capture stage. Don't read any xforms here.
    if (!_stage) _stage = _sGlobalStage;
    if (!_stage) {
        const char* p = std::getenv("HDLOD_STAGE_PATH");
        if (p && p[0]) _stage = UsdStage::Open(p);
    }
    if (_stage) {
        TF_STATUS("hdLod: stage captured");
        _RebuildGroupCache();
    }
}

// ---------------------------------------------------------------------------
// GetPrim — pure overlay, no side effects
// ---------------------------------------------------------------------------

HdSceneIndexPrim
HdLodSceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (_hiddenRenderables.count(primPath) && prim.dataSource) {
        prim.dataSource = _InvisibleDataSource::New(prim.dataSource);
    }
    return prim;
}

SdfPathVector
HdLodSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const {
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

// ---------------------------------------------------------------------------
// _PrimsAdded — rebuild caches, NO xform reads
// ---------------------------------------------------------------------------

void HdLodSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    // Try stage capture if we don't have one yet
    if (!_stage && _sGlobalStage) {
        _stage = _sGlobalStage;
        TF_STATUS("hdLod: stage captured on PrimsAdded");
    }

    if (_stage) {
        _RebuildGroupCache();
    }

    // Discover camera path from added prims — only accept cameras on the stage
    if (_stage) {
        for (const auto &e : entries) {
            const auto &input = _GetInputSceneIndex();
            HdSceneIndexPrim p = input->GetPrim(e.primPath);
            if (p.primType == TfToken("camera") && _cameraPath.IsEmpty()) {
                // Only accept cameras that exist on the USD stage
                if (_stage->GetPrimAtPath(e.primPath)) {
                    _cameraPath = e.primPath;
                    TF_STATUS("hdLod: camera discovered: %s",
                        _cameraPath.GetText());
                }
            }
        }
    }

    _SendPrimsAdded(entries);
}

// ---------------------------------------------------------------------------
// _PrimsRemoved
// ---------------------------------------------------------------------------

void HdLodSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    for (const auto &e : entries) {
        _lodGroups.erase(e.primPath);
        _descendantCache.erase(e.primPath);
        _hiddenRenderables.erase(e.primPath);
        if (e.primPath == _cameraPath) {
            _cameraPath = SdfPath();
        }
    }
    _SendPrimsRemoved(entries);
}

// ---------------------------------------------------------------------------
// _PrimsDirtied — safe to read xforms here (data source is valid)
// ---------------------------------------------------------------------------

void HdLodSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    bool needsEval = false;

    for (const auto &e : entries) {
        for (const auto &loc : e.dirtyLocators) {
            if (!loc.IsEmpty() &&
                loc.GetFirstElement() == HdXformSchemaTokens->xform) {
                // Camera xform changed — update position
                if (e.primPath == _cameraPath) {
                    _UpdateCameraPosition();
                    needsEval = true;
                }
                break;
            }
        }
    }

    // Also evaluate on first dirty after prims are fully added
    if (!_lodInitialized && !_lodGroups.empty() && !_cameraPath.IsEmpty()) {
        _UpdateCameraPosition();
        needsEval = true;
        _lodInitialized = true;
    }

    if (needsEval) {
        _EvaluateLod();
    }

    _SendPrimsDirtied(entries);
}

// ---------------------------------------------------------------------------
// _UpdateCameraPosition — reads xform from scene index (safe in _PrimsDirtied)
// ---------------------------------------------------------------------------

void HdLodSceneIndex::_UpdateCameraPosition()
{
    if (_cameraPath.IsEmpty()) return;

    // Read camera position from the USD stage.
    // The Hydra flattened xform data source has a bug in our fork where
    // _MatrixCombinerDataSource's initializer dereferences parent handles
    // without null checks (see issue #22). Using ComputeLocalToWorldTransform
    // from the USD stage is equivalent and safe.
    // TODO: Switch to Hydra xform read once #22 is fixed.
    if (_stage) {
        UsdPrim camPrim = _stage->GetPrimAtPath(_cameraPath);
        if (camPrim) {
            UsdGeomXformable xf(camPrim);
            _cachedCameraPos = xf.ComputeLocalToWorldTransform(
                UsdTimeCode::Default()).ExtractTranslation();
        }
    }
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

bool HdLodSceneIndex::_IsRenderable(const TfToken &pt) {
    if (HdPrimTypeIsGprim(pt)) return true;
    static const TfTokenVector implicits = {
        TfToken("sphere"), TfToken("cube"), TfToken("cone"),
        TfToken("cylinder"), TfToken("capsule"), TfToken("plane"),
    };
    return std::find(implicits.begin(), implicits.end(), pt)
        != implicits.end();
}

void HdLodSceneIndex::_CollectRenderables(
    const SdfPath &path, std::vector<SdfPath> &out) const
{
    const auto &input = _GetInputSceneIndex();
    HdSceneIndexPrim p = input->GetPrim(path);
    if (_IsRenderable(p.primType)) out.push_back(path);
    for (const SdfPath &c : input->GetChildPrimPaths(path))
        _CollectRenderables(c, out);
}

void HdLodSceneIndex::_RebuildGroupCache()
{
    _lodGroups.clear();
    _descendantCache.clear();

    if (!_stage) return;

    for (UsdPrim prim : _stage->Traverse()) {
        UsdRelationship rel = prim.GetRelationship(TfToken("lod:lodItems"));
        if (!rel || !rel.HasAuthoredTargets()) continue;

        SdfPathVector targets;
        rel.GetTargets(&targets);
        if (targets.empty()) continue;

        _GroupData gd;
        gd.lodItems = {targets.begin(), targets.end()};

        for (UsdAttribute attr : prim.GetAttributes()) {
            const std::string &n = attr.GetName().GetString();
            VtArray<float> val;
            if (TfStringStartsWith(n, "lod:Heuristic:") &&
                TfStringEndsWith(n, ":distance:minThresholds") &&
                attr.Get(&val))
                gd.minThresholds = val;
            else if (TfStringStartsWith(n, "lod:Heuristic:") &&
                     TfStringEndsWith(n, ":distance:maxThresholds") &&
                     attr.Get(&val))
                gd.maxThresholds = val;
        }
        if (gd.maxThresholds.empty() && !gd.minThresholds.empty())
            gd.maxThresholds = gd.minThresholds;

        _lodGroups[prim.GetPath()] = std::move(gd);
    }

    // Collect renderables per item
    for (const auto &[gp, gd] : _lodGroups) {
        for (const SdfPath &ip : gd.lodItems) {
            std::vector<SdfPath> renderables;
            _CollectRenderables(ip, renderables);
            _descendantCache[ip] = std::move(renderables);
        }
    }

    TF_STATUS("hdLod: %zu groups, %zu items cached",
        _lodGroups.size(), _descendantCache.size());
}

// ---------------------------------------------------------------------------
// LOD evaluation (called from _PrimsDirtied only)
// ---------------------------------------------------------------------------

void HdLodSceneIndex::_EvaluateLod()
{
    std::unordered_set<SdfPath, SdfPath::Hash> newHidden;
    std::unordered_set<SdfPath, SdfPath::Hash> inactiveSubtrees;

    for (const auto &[groupPath, gd] : _lodGroups) {
        if (gd.lodItems.empty()) continue;

        // Axiom 1: hierarchical gating
        bool skip = false;
        for (const auto &is : inactiveSubtrees)
            if (groupPath.HasPrefix(is)) { skip = true; break; }
        if (skip) {
            for (const auto &ip : gd.lodItems) {
                auto it = _descendantCache.find(ip);
                if (it != _descendantCache.end())
                    for (const auto &r : it->second) newHidden.insert(r);
            }
            continue;
        }

        // Group position — read from USD stage (Hydra xform bug, issue #22)
        GfVec3d groupPos(0, 0, 0);
        if (_stage) {
            UsdPrim gp = _stage->GetPrimAtPath(groupPath);
            if (gp) {
                UsdGeomXformable xf(gp);
                groupPos = xf.ComputeLocalToWorldTransform(
                    UsdTimeCode::Default()).ExtractTranslation();
            }
        }

        double dist = (_cachedCameraPos - groupPos).GetLength();
        int nItems = (int)gd.lodItems.size();
        int nThresh = (int)gd.minThresholds.size();
        int active = 0;

        if (nThresh > 0) {
            auto prevIt = _prevActiveIndex.find(groupPath);
            int prev = (prevIt != _prevActiveIndex.end()) ? prevIt->second : -1;

            if (prev < 0) {
                active = nItems - 1;
                for (int i = 0; i < nThresh; ++i)
                    if (dist < (double)gd.minThresholds[i]) { active = i; break; }
            } else {
                int maxI = nItems - 1;
                for (int i = 0; i < nThresh && i < (int)gd.maxThresholds.size(); ++i)
                    if (dist < (double)gd.maxThresholds[i]) { maxI = i; break; }
                int minI = nItems - 1;
                for (int i = 0; i < nThresh; ++i)
                    if (dist < (double)gd.minThresholds[i]) { minI = i; break; }
                if (maxI > prev) active = maxI;
                else if (minI < prev) active = minI;
                else active = prev;
            }
        }
        active = std::max(0, std::min(active, nItems - 1));
        _prevActiveIndex[groupPath] = active;

        for (int i = 0; i < nItems; ++i) {
            if (i != active) inactiveSubtrees.insert(gd.lodItems[i]);
        }
        for (int i = 0; i < nItems; ++i) {
            if (i == active) continue;
            auto it = _descendantCache.find(gd.lodItems[i]);
            if (it != _descendantCache.end())
                for (const auto &r : it->second) newHidden.insert(r);
        }
    }

    // Diff and dirty
    HdSceneIndexObserver::DirtiedPrimEntries dirty;
    static const HdDataSourceLocatorSet visLoc{
        HdVisibilitySchema::GetDefaultLocator()};

    for (const auto &p : _hiddenRenderables)
        if (!newHidden.count(p)) dirty.push_back({p, visLoc});
    for (const auto &p : newHidden)
        if (!_hiddenRenderables.count(p)) dirty.push_back({p, visLoc});

    _hiddenRenderables = std::move(newHidden);

    if (!dirty.empty()) {
        TF_STATUS("hdLod: LOD updated, %zu hidden, %zu dirty, cam=(%.1f,%.1f,%.1f)",
            _hiddenRenderables.size(), dirty.size(),
            _cachedCameraPos[0], _cachedCameraPos[1], _cachedCameraPos[2]);
        _SendPrimsDirtied(dirty);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

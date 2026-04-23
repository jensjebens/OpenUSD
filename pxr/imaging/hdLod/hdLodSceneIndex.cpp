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
#include "pxr/imaging/hd/overlayContainerDataSource.h"
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
// Singleton invisible visibility overlay (issue #43)
// ---------------------------------------------------------------------------
namespace {

// Static visibility=false data source, shared across all hidden prims.
// HdOverlayContainerDataSource checks sources in order — first hit wins.
// "visibility" → hits this overlay. Anything else → falls through to
// the original prim data source.
static const HdContainerDataSourceHandle &_GetInvisibleOverlay()
{
    static HdContainerDataSourceHandle s =
        HdRetainedContainerDataSource::New(
            HdVisibilitySchemaTokens->visibility,
            HdRetainedContainerDataSource::New(
                HdVisibilitySchemaTokens->visibility,
                HdRetainedTypedSampledDataSource<bool>::New(false)));
    return s;
}

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
    // Lazy LOD re-evaluation: check the camera's flattened xform once per
    // render pass. Uses a reentrancy guard to prevent infinite loops
    // (_EvaluateLod sends dirty → triggers render → GetPrim → evaluate...)
    if (_stage && !_cameraPath.IsEmpty() && !_lodGroups.empty()
        && !_evaluatingLod) {
        const auto &input = _GetInputSceneIndex();
        HdSceneIndexPrim camPrim = input->GetPrim(_cameraPath);
        if (camPrim.dataSource) {
            HdXformSchema xs =
                HdXformSchema::GetFromParent(camPrim.dataSource);
            if (xs.IsDefined()) {
                HdMatrixDataSourceHandle matDs = xs.GetMatrix();
                if (matDs) {
                    GfVec3d pos = matDs->GetTypedValue(0.0f)
                        .ExtractTranslation();
                    if (pos != _cachedCameraPos) {
                        auto *self = const_cast<HdLodSceneIndex *>(this);
                        self->_evaluatingLod = true;
                        self->_cachedCameraPos = pos;
                        self->_EvaluateLod();
                        self->_evaluatingLod = false;
                    }
                }
            }
        }
    }

    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (_hiddenRenderables.count(primPath) && prim.dataSource) {
        prim.dataSource = HdOverlayContainerDataSource::New(
            _GetInvisibleOverlay(), prim.dataSource);
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

    // Re-evaluate LOD on PrimsAdded — UsdView frame scrubbing sends
    // PrimsAdded (not PrimsDirtied) when animated prims change.
    if (!_cameraPath.IsEmpty() && _stage) {
        _UpdateCameraPosition();
        _EvaluateLod();
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
// Check if ANY prim has an xform dirty signal — this fires on every
    // frame during UsdView playback when animated prims change.
    // Don't filter by camera path: the viewport camera may be a free-cam
    // that doesn't correspond to a USD prim, and animated objects (not the
    // camera) are what get dirtied during scrubbing.
    // (Pattern from Newton's HdExec scene index plugin.)
    bool xformDirty = false;
    for (const auto &e : entries) {
        if (e.dirtyLocators.Contains(
                HdXformSchema::GetDefaultLocator())) {
            xformDirty = true;
            break;
        }
    }

    if (xformDirty && !_cameraPath.IsEmpty()) {
        GfVec3d prevPos = _cachedCameraPos;
        _UpdateCameraPosition();
        // Only re-evaluate LOD if the camera actually moved (issue #42).
        // Without this guard, every xform dirty (physics bodies, animated
        // characters) would trigger a full LOD re-evaluation.
        if ((_cachedCameraPos - prevPos).GetLengthSq() > 1e-12) {
            _EvaluateLod();
        }
    }

    // Also evaluate on first dirty after prims are fully added
    if (!_lodInitialized && !_lodGroups.empty() && !_cameraPath.IsEmpty()) {
        _UpdateCameraPosition();
        _EvaluateLod();
        _lodInitialized = true;
    }

    _SendPrimsDirtied(entries);
}

// ---------------------------------------------------------------------------
// _UpdateCameraPosition — reads xform from scene index (safe in _PrimsDirtied)
// ---------------------------------------------------------------------------

void HdLodSceneIndex::_UpdateCameraPosition()
{
    if (_cameraPath.IsEmpty()) return;

    // Read camera world-space position from Hydra's flattened xform.
    // Post-flattening scene index plugins receive world-space matrices
    // via HdXformSchema after HdFlatteningSceneIndex (step 6).
    const auto &input = _GetInputSceneIndex();
    HdSceneIndexPrim camPrim = input->GetPrim(_cameraPath);

    if (camPrim.dataSource) {
        HdXformSchema xs = HdXformSchema::GetFromParent(camPrim.dataSource);
        if (xs.IsDefined()) {
            HdMatrixDataSourceHandle matDs = xs.GetMatrix();
            if (matDs) {
                GfMatrix4d mat = matDs->GetTypedValue(0.0f);
                _cachedCameraPos = mat.ExtractTranslation();
                return;
            }
        }
    }

    // Fallback: read from USD stage (e.g. during initial scene population
    // when Hydra data sources may not be fully initialized)
    if (_stage) {
        UsdPrim usdPrim = _stage->GetPrimAtPath(_cameraPath);
        if (usdPrim) {
            UsdGeomXformable xf(usdPrim);
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

        // Group position — read from Hydra's flattened xform (issue #41).
        // Post-flattening plugins receive world-space matrices via
        // HdXformSchema.  GetTypedValue(0) is a shutter offset (current
        // frame, no motion blur), NOT a USD time code.
        // Read from _GetInputSceneIndex() (upstream) to avoid recursing
        // into our own visibility overlay.
        GfVec3d groupPos(0, 0, 0);
        {
            const auto &input = _GetInputSceneIndex();
            HdSceneIndexPrim gPrim = input->GetPrim(groupPath);
            if (gPrim.dataSource) {
                HdXformSchema xs =
                    HdXformSchema::GetFromParent(gPrim.dataSource);
                if (xs.IsDefined()) {
                    HdMatrixDataSourceHandle matDs = xs.GetMatrix();
                    if (matDs) {
                        groupPos = matDs->GetTypedValue(0.0f)
                            .ExtractTranslation();
                    }
                }
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

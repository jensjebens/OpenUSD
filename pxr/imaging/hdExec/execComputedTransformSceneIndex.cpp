//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/prim.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/vt/value.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(
    HDEXEC_AUTO_BOOTSTRAP, true,
    "Enable auto-bootstrapping of the HdExec scene index filter. "
    "When enabled, the filter lazily detects a UsdStage and creates "
    "its own ExecUsdSystem.");

// ---------------------------------------------------------------------------
// Static members
// ---------------------------------------------------------------------------

std::mutex HdExecComputedTransformSceneIndex::_sGlobalStageMutex;
UsdStageRefPtr HdExecComputedTransformSceneIndex::_sGlobalStage;

// Track all auto-bootstrapped instances for global time advancing
static std::mutex _sInstancesMutex;
static std::vector<HdExecComputedTransformSceneIndex*> _sInstances;

void
HdExecComputedTransformSceneIndex::SetGlobalStage(
    const UsdStageRefPtr &stage)
{
    std::lock_guard<std::mutex> lock(_sGlobalStageMutex);
    _sGlobalStage = stage;
    fprintf(stderr, "[HdExec] SetGlobalStage: %s\n",
            stage ? stage->GetRootLayer()->GetIdentifier().c_str() : "null");
}

void
HdExecComputedTransformSceneIndex::AdvanceGlobalTime(UsdTimeCode time)
{
    std::lock_guard<std::mutex> lock(_sInstancesMutex);
    for (auto *inst : _sInstances) {
        if (inst->_bootstrapped && inst->_execSystem) {
            fprintf(stderr, "[HdExec] AdvanceGlobalTime: frame=%.1f\n",
                    time.GetValue());
            inst->_execSystem->ChangeTime(time);
            inst->_currentTime = time;
            inst->_currentTimeFrame = time.IsDefault() ? 0.0 : time.GetValue();
            
            // Dirty all computed prims so Hydra re-pulls GetPrim()
            HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;
            {
                std::lock_guard<std::mutex> cacheLock(inst->_cacheMutex);
                for (const auto &pair : inst->_hasComputationCache) {
                    if (pair.second) {
                        dirtyEntries.push_back({
                            pair.first,
                            HdDataSourceLocatorSet{
                                HdXformSchema::GetDefaultLocator()}});
                    }
                }
            }
            if (!dirtyEntries.empty()) {
                inst->_SendPrimsDirtied(dirtyEntries);
            }
        }
    }
}

namespace
{

// ---------------------------------------------------------------------------
// _ExecMatrixDataSource
// ---------------------------------------------------------------------------
// A data source that evaluates an OpenExec computation to produce a GfMatrix4d
// transform value. This is the heart of the bridge between exec and Hydra.

class _ExecMatrixDataSource : public HdMatrixDataSource
{
public:
    HD_DECLARE_DATASOURCE(_ExecMatrixDataSource);

    VtValue GetValue(const Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    GfMatrix4d GetTypedValue(const Time shutterOffset) override
    {
        // Build request and compute.
        std::vector<ExecUsdValueKey> keys;
        keys.emplace_back(_prim, _computationToken);

        ExecUsdRequest request =
            _execSystem->BuildRequest(std::move(keys));
        if (!request.IsValid()) {
            return GfMatrix4d(1.0);
        }

        _execSystem->PrepareRequest(request);
        ExecUsdCacheView view = _execSystem->Compute(request);

        VtValue val = view.Get(0);
        if (val.IsHolding<GfMatrix4d>()) {
            return val.UncheckedGet<GfMatrix4d>();
        }

        return GfMatrix4d(1.0);
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        // Single sample — no motion blur for exec computations (for now).
        return false;
    }

private:
    _ExecMatrixDataSource(
        ExecUsdSystem *execSystem,
        const UsdPrim &prim,
        const TfToken &computationToken)
        : _execSystem(execSystem)
        , _prim(prim)
        , _computationToken(computationToken)
    {
    }

    ExecUsdSystem *_execSystem;
    UsdPrim _prim;
    TfToken _computationToken;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// HdExecComputedTransformSceneIndex
// ---------------------------------------------------------------------------

/* static */
HdExecComputedTransformSceneIndexRefPtr
HdExecComputedTransformSceneIndex::New(
    const HdSceneIndexBaseRefPtr &inputSceneIndex,
    const UsdStageConstRefPtr &stage,
    std::shared_ptr<ExecUsdSystem> execSystem,
    const TfTokenVector &computationTokens,
    bool resetXformStack)
{
    return TfCreateRefPtr(
        new HdExecComputedTransformSceneIndex(
            inputSceneIndex, stage, std::move(execSystem),
            computationTokens, resetXformStack));
}

/* static */
HdExecComputedTransformSceneIndexRefPtr
HdExecComputedTransformSceneIndex::NewAutoBootstrap(
    const HdSceneIndexBaseRefPtr &inputSceneIndex,
    const TfTokenVector &computationTokens,
    bool resetXformStack)
{
    auto si = TfCreateRefPtr(
        new HdExecComputedTransformSceneIndex(
            inputSceneIndex,
            /* stage = */ nullptr,
            /* execSystem = */ nullptr,
            computationTokens,
            resetXformStack));
    si->_autoBootstrap = true;

    // Register for UsdNotice::StageContentsChanged to discover stages.
    // The notice fires whenever a UsdStage's contents change — including
    // after initial population when UsdImagingStageSceneIndex::SetStage()
    // triggers _Populate().
    si->_stageNoticeKey = TfNotice::Register(
        TfCreateWeakPtr(si.operator->()),
        &HdExecComputedTransformSceneIndex::_OnStageContentsChanged);

    return si;
}

HdExecComputedTransformSceneIndex::HdExecComputedTransformSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex,
    const UsdStageConstRefPtr &stage,
    std::shared_ptr<ExecUsdSystem> execSystem,
    const TfTokenVector &computationTokens,
    bool resetXformStack)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    , _stage(stage)
    , _execSystem(std::move(execSystem))
    , _computationTokens(computationTokens)
    , _resetXformStack(resetXformStack)
    , _currentTime(UsdTimeCode::Default())
{
    // Register this instance for global time advancing
    std::lock_guard<std::mutex> lock(_sInstancesMutex);
    _sInstances.push_back(this);
}

HdExecComputedTransformSceneIndex::~HdExecComputedTransformSceneIndex()
{
    // Unregister from global instances
    std::lock_guard<std::mutex> lock(_sInstancesMutex);
    for (auto it = _sInstances.begin(); it != _sInstances.end(); ++it) {
        if (*it == this) {
            _sInstances.erase(it);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Auto-bootstrap implementation
// ---------------------------------------------------------------------------

void
HdExecComputedTransformSceneIndex::_OnStageContentsChanged(
    const UsdNotice::StageContentsChanged &notice)
{
    if (_bootstrapped || !_autoBootstrap) {
        return;
    }

    UsdStageWeakPtr sender = notice.GetStage();
    if (!sender) {
        return;
    }

    // Capture the stage in the static global for this and other instances.
    UsdStageRefPtr stage(sender);
    SetGlobalStage(stage);

    // Try to bootstrap now that we have a stage.
    _TryBootstrap();
}

void
HdExecComputedTransformSceneIndex::_TryBootstrap() const
{
    if (_bootstrapped) {
        return;
    }

    if (!TfGetEnvSetting(HDEXEC_AUTO_BOOTSTRAP)) {
        fprintf(stderr, "[HdExec] _TryBootstrap: disabled by env var\n");
        return;
    }

    UsdStageRefPtr stage;
    {
        std::lock_guard<std::mutex> lock(_sGlobalStageMutex);
        if (!_sGlobalStage) {
            fprintf(stderr, "[HdExec] _TryBootstrap: no global stage yet\n");
            return;
        }
        stage = _sGlobalStage;
    }

    TF_STATUS("HdExec: Auto-bootstrapping with stage @%s@",
              stage->GetRootLayer()->GetIdentifier().c_str());
    fprintf(stderr, "[HdExec] _TryBootstrap: got stage @%s@\n",
            stage->GetRootLayer()->GetIdentifier().c_str());

    // Create the exec system for this stage.
    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    // These members are mutable — safe to assign from const context.
    _stage = stage;
    _execSystem = std::move(execSystem);
    _bootstrapped = true;

    TF_STATUS("HdExec: Auto-bootstrap complete — exec system ready");
    fprintf(stderr, "[HdExec] Bootstrap COMPLETE! ExecUsdSystem ready.\n");
}

// ---------------------------------------------------------------------------
// Scene index overrides
// ---------------------------------------------------------------------------

HdSceneIndexPrim
HdExecComputedTransformSceneIndex::GetPrim(const SdfPath &primPath) const
{
    // Lazy bootstrap on first access.
    if (_autoBootstrap && !_bootstrapped) {
        _TryBootstrap();
    }

    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (_bootstrapped && _HasExecComputation(primPath)) {
        fprintf(stderr, "[HdExec] GetPrim: OVERLAY applied for %s\n",
                primPath.GetText());
        prim.dataSource = HdOverlayContainerDataSource::New(
            _CreateExecXformDataSource(primPath),
            prim.dataSource);
    }

    return prim;
}

SdfPathVector
HdExecComputedTransformSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdExecComputedTransformSceneIndex::SetTime(UsdTimeCode time)
{
    if (!_execSystem) {
        return;
    }

    _currentTime = time;
    _execSystem->ChangeTime(time);

    // Dirty all prims that have exec computations.
    HdSceneIndexObserver::DirtiedPrimEntries entries;
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        for (const auto &pair : _hasComputationCache) {
            if (pair.second) {
                entries.push_back({
                    pair.first,
                    HdDataSourceLocatorSet{
                        HdXformSchema::GetDefaultLocator()}});
            }
        }
    }

    if (!entries.empty()) {
        _SendPrimsDirtied(entries);
    }
}

SdfPathVector
HdExecComputedTransformSceneIndex::GetComputedPrimPaths() const
{
    SdfPathVector result;
    std::lock_guard<std::mutex> lock(_cacheMutex);
    for (const auto &pair : _hasComputationCache) {
        if (pair.second) {
            result.push_back(pair.first);
        }
    }
    return result;
}

bool
HdExecComputedTransformSceneIndex::_HasExecComputation(
    const SdfPath &primPath) const
{
    if (!_execSystem || !_stage) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _hasComputationCache.find(primPath);
        if (it != _hasComputationCache.end()) {
            return it->second;
        }
    }

    UsdPrim prim = _stage->GetPrimAtPath(primPath);
    if (!prim) {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        _hasComputationCache[primPath] = false;
        return false;
    }

    // Try each computation token.
    for (const auto &token : _computationTokens) {
        std::vector<ExecUsdValueKey> keys;
        keys.emplace_back(prim, token);
        ExecUsdRequest req = _execSystem->BuildRequest(std::move(keys));
        fprintf(stderr, "[HdExec] _HasExecComputation: %s token=%s valid=%d\n",
                primPath.GetText(), token.GetText(), req.IsValid());
        if (req.IsValid()) {
            std::lock_guard<std::mutex> lock(_cacheMutex);
            _hasComputationCache[primPath] = true;
            return true;
        }
    }

    std::lock_guard<std::mutex> lock(_cacheMutex);
    _hasComputationCache[primPath] = false;
    return false;
}

HdContainerDataSourceHandle
HdExecComputedTransformSceneIndex::_CreateExecXformDataSource(
    const SdfPath &primPath) const
{
    UsdPrim prim = _stage->GetPrimAtPath(primPath);

    // Find the first valid computation token for this prim.
    TfToken activeToken;
    for (const auto &token : _computationTokens) {
        std::vector<ExecUsdValueKey> keys;
        keys.emplace_back(prim, token);
        ExecUsdRequest req = _execSystem->BuildRequest(std::move(keys));
        if (req.IsValid()) {
            activeToken = token;
            break;
        }
    }

    return HdXformSchema::Builder()
        .SetMatrix(
            _ExecMatrixDataSource::New(
                _execSystem.get(), prim, activeToken))
        .SetResetXformStack(
            HdRetainedTypedSampledDataSource<bool>::New(_resetXformStack))
        .Build();
}

void
HdExecComputedTransformSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    // Auto-bootstrap: when prims are first added, the stage is available.
    // This is the right time to bootstrap because _PrimsAdded fires
    // AFTER the stage scene index populates, which means the stage
    // is fully set up and the notice we missed at construction time
    // doesn't matter.
    if (_autoBootstrap && !_bootstrapped) {
        _TryBootstrap();
    }

    // Invalidate cache for added paths.
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        for (const auto &entry : entries) {
            _hasComputationCache.erase(entry.primPath);
        }
    }

    _SendPrimsAdded(entries);
}

void
HdExecComputedTransformSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    // Remove from cache.
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        for (const auto &entry : entries) {
            _hasComputationCache.erase(entry.primPath);
        }
    }

    _SendPrimsRemoved(entries);
}

void
HdExecComputedTransformSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    // In auto-bootstrap mode, detect time changes from upstream xform
    // dirtying. When UsdImagingStageSceneIndex::SetTime() is called,
    // it dirties xforms on all time-dependent prims. We intercept that
    // to step our exec system and re-evaluate physics.
    if (_bootstrapped && _autoBootstrap && _execSystem) {
        bool xformDirty = false;
        for (const auto &entry : entries) {
            if (entry.dirtyLocators.Contains(
                    HdXformSchema::GetDefaultLocator())) {
                xformDirty = true;
                break;
            }
        }

        if (xformDirty) {
            // The upstream stage scene index has changed time.
            // Track frame count and advance the exec system's time.
            // UsdImagingStageSceneIndex calls SetTime() before dirtying,
            // so the stage prims now have new time-sampled values.
            //
            // We use the stage's timeCodesPerSecond to convert from
            // frame count to UsdTimeCode. We increment a frame counter
            // each time we detect a time-change dirty.
            _currentTimeFrame += 1.0;
            UsdTimeCode newTime(_currentTimeFrame);
            fprintf(stderr, "[HdExec] Time change detected, advancing to frame %.0f\n",
                    _currentTimeFrame);
            _execSystem->ChangeTime(newTime);
            _currentTime = newTime;
            
            // Also step Newton physics
            if (_stage) {
                double fps = _stage->GetTimeCodesPerSecond();
                if (fps <= 0) fps = 24.0;
                double timeInSeconds = _currentTimeFrame / fps;
                // Import Newton header indirectly via the computation
                // Newton stepping happens in the computation callback
                // via computeTime. But we need ChangeTime first so
                // computeTime returns the new value.
            }
        }
    }

    _SendPrimsDirtied(entries);
}

PXR_NAMESPACE_CLOSE_SCOPE

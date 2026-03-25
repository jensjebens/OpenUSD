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

#include "pxr/usd/usd/prim.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/vt/value.h"

PXR_NAMESPACE_OPEN_SCOPE

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
{
}

HdSceneIndexPrim
HdExecComputedTransformSceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (_HasExecComputation(primPath)) {
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

    _SendPrimsDirtied(entries);
}

PXR_NAMESPACE_CLOSE_SCOPE

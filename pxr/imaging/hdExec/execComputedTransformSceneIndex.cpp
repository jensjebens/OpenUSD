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
#include "pxr/usd/usd/schemaRegistry.h"

#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"

#include "pxr/base/js/json.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/errorMark.h"
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
static std::atomic<double> _sGlobalTimeFrame{0.0};

// Static transform provider registry (keyed by name)
static std::mutex _sTransformProviderMutex;
static std::vector<std::pair<TfToken,
    HdExecComputedTransformSceneIndex::TransformProviderFn>> _sTransformProviders;

void
HdExecComputedTransformSceneIndex::RegisterTransformProvider(
    const TfToken &name, TransformProviderFn fn)
{
    std::lock_guard<std::mutex> lock(_sTransformProviderMutex);
    // Replace if name already exists.
    for (auto &entry : _sTransformProviders) {
        if (entry.first == name) {
            entry.second = std::move(fn);
            return;
        }
    }
    _sTransformProviders.emplace_back(name, std::move(fn));
}

void
HdExecComputedTransformSceneIndex::UnregisterTransformProvider(
    const TfToken &name)
{
    std::lock_guard<std::mutex> lock(_sTransformProviderMutex);
    _sTransformProviders.erase(
        std::remove_if(_sTransformProviders.begin(), _sTransformProviders.end(),
            [&](const auto &entry) { return entry.first == name; }),
        _sTransformProviders.end());
}

GfMatrix4d
HdExecComputedTransformSceneIndex::QueryTransformProviders(
    const SdfPath &primPath, double timeSeconds)
{
    static const GfMatrix4d identity(1.0);
    std::lock_guard<std::mutex> lock(_sTransformProviderMutex);
    for (const auto &entry : _sTransformProviders) {
        GfMatrix4d result = entry.second(primPath, timeSeconds);
        if (result != identity) {
            return result;
        }
    }
    return identity;
}

void
HdExecComputedTransformSceneIndex::SetGlobalStage(
    const UsdStageRefPtr &stage)
{
    std::lock_guard<std::mutex> lock(_sGlobalStageMutex);
    _sGlobalStage = stage;
}

UsdStageRefPtr
HdExecComputedTransformSceneIndex::GetGlobalStage()
{
    std::lock_guard<std::mutex> lock(_sGlobalStageMutex);
    return _sGlobalStage;
}

double
HdExecComputedTransformSceneIndex::GetGlobalTimeFrame()
{
    return _sGlobalTimeFrame.load();
}

void
HdExecComputedTransformSceneIndex::AdvanceGlobalTime(UsdTimeCode time)
{
    std::lock_guard<std::mutex> lock(_sInstancesMutex);
    _sGlobalTimeFrame.store(time.IsDefault() ? 0.0 : time.GetValue());
    for (auto *inst : _sInstances) {
        if (inst->_bootstrapped && inst->_execSystem) {
            inst->_execSystem->ChangeTime(time);
            inst->_currentTime = time;
            inst->_currentTimeFrame = time.IsDefault() ? 0.0 : time.GetValue();
            
            // Dirty all computed prims so Hydra re-pulls GetPrim()
            // Use UniversalSet to ensure the CachingSceneIndex evicts
            // its cache (it only evicts on container-level dirty, not
            // on specific locators like xform).
            HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;
            {
                std::lock_guard<std::mutex> cacheLock(inst->_cacheMutex);
                for (const auto &pair : inst->_hasComputationCache) {
                    if (pair.second) {
                        dirtyEntries.push_back({
                            pair.first,
                            HdDataSourceLocatorSet::UniversalSet()});
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
// A data source that provides simulated transforms for physics prims.
//
// If a TransformProvider callback is registered (by the physics plugin),
// it is used directly — this bypasses exec's computation cache, which
// doesn't invalidate for side-effect-driven computations like physics.
//
// If no provider is registered, falls back to exec system evaluation.

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
        // Query registered transform providers (e.g. physics engines).
        // Providers bypass exec's computation cache for side-effect-driven
        // transforms that change every frame.
        {
            double frame =
                HdExecComputedTransformSceneIndex::GetGlobalTimeFrame();
            double fps = 60.0;
            if (_stage) {
                fps = _stage->GetTimeCodesPerSecond();
                if (fps <= 0) fps = 60.0;
            }
            double timeSeconds = frame / fps;

            static const GfMatrix4d identity(1.0);
            GfMatrix4d mat =
                HdExecComputedTransformSceneIndex::QueryTransformProviders(
                    _primPath, timeSeconds);
            if (mat != identity) {
                return mat;
            }
        }

        // Fallback: exec system evaluation (may be cached).
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
        return false;
    }

private:
    _ExecMatrixDataSource(
        ExecUsdSystem *execSystem,
        const UsdPrim &prim,
        const TfToken &computationToken)
        : _execSystem(execSystem)
        , _prim(prim)
        , _primPath(prim.GetPath())
        , _stage(prim.GetStage())
        , _computationToken(computationToken)
    {
    }

    ExecUsdSystem *_execSystem;
    UsdPrim _prim;
    SdfPath _primPath;
    UsdStageRefPtr _stage;
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
        return;
    }

    UsdStageRefPtr stage;
    {
        std::lock_guard<std::mutex> lock(_sGlobalStageMutex);
        if (!_sGlobalStage) {
            return;
        }
        stage = _sGlobalStage;
    }

    TF_STATUS("HdExec: Auto-bootstrapping with stage @%s@",
              stage->GetRootLayer()->GetIdentifier().c_str());

    // Force-load any discovered exec computation plugins so
    // EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA runs before we create
    // the ExecUsdSystem. Without this, computations from dynamically
    // loaded plugins won't be available.
    {
        PlugRegistry &reg = PlugRegistry::GetInstance();
        for (const auto &plugin : reg.GetAllPlugins()) {
            if (!plugin->IsLoaded() && plugin->GetMetadata().count("Exec")) {
                plugin->Load();
            }
        }
    }

    // Create the exec system for this stage.
    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    // These members are mutable — safe to assign from const context.
    _stage = stage;
    _execSystem = std::move(execSystem);
    _bootstrapped = true;

    TF_STATUS("HdExec: Auto-bootstrap complete — exec system ready");
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

    // Check if the prim has an applied API schema that is registered
    // for exec computations. We inspect plugin metadata for
    // "allowsPluginComputations" entries to determine which schemas
    // provide computations, then check if the prim has any of them.
    //
    // This is more reliable than BuildRequest/PrepareRequest probing,
    // which can succeed for prims that don't actually have the schema,
    // leading to exec compilation failures at render time.
    {
        static std::mutex sSchemasMutex;
        static std::vector<TfToken> sComputationSchemas;
        static bool sLoaded = false;

        std::lock_guard<std::mutex> schemasLock(sSchemasMutex);
        if (!sLoaded) {
            for (const auto &plugin :
                     PlugRegistry::GetInstance().GetAllPlugins()) {
                JsObject metadata = plugin->GetMetadata();
                auto execIt = metadata.find("Exec");
                if (execIt == metadata.end() ||
                    !execIt->second.IsObject()) {
                    continue;
                }
                const JsObject &execObj = execIt->second.GetJsObject();
                auto schemasIt = execObj.find("Schemas");
                if (schemasIt == execObj.end() ||
                    !schemasIt->second.IsObject()) {
                    continue;
                }
                for (const auto &entry :
                         schemasIt->second.GetJsObject()) {
                    if (entry.second.IsObject()) {
                        const JsObject &schemaObj =
                            entry.second.GetJsObject();
                        auto apcIt =
                            schemaObj.find("allowsPluginComputations");
                        if (apcIt != schemaObj.end() &&
                            apcIt->second.IsBool() &&
                            apcIt->second.GetBool()) {
                            sComputationSchemas.push_back(
                                TfToken(entry.first));
                        }
                    }
                }
            }
            sLoaded = true;
        }

        bool hasSchema = false;
        TfTokenVector appliedSchemas = prim.GetAppliedSchemas();
        for (const auto &schemaClassName : sComputationSchemas) {
            // plugInfo uses C++ class names (e.g. "UsdPhysicsRigidBodyAPI").
            // Prim's applied schemas use short identifiers (e.g.
            // "PhysicsRigidBodyAPI"). Look up via TfType → SchemaRegistry.
            const TfType &schemaType =
                TfType::FindByName(schemaClassName.GetString());
            if (!schemaType.IsUnknown()) {
                const UsdSchemaRegistry::SchemaInfo *info =
                    UsdSchemaRegistry::FindSchemaInfo(schemaType);
                if (info) {
                    if (prim.HasAPI(info->identifier)) {
                        hasSchema = true;
                        break;
                    }
                }
            }
        }
        if (!hasSchema) {
            std::lock_guard<std::mutex> lock(_cacheMutex);
            _hasComputationCache[primPath] = false;
            return false;
        }
    }

    // Try each computation token via exec.
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

    // Wrap the xform schema container under the "xform" key so it
    // overlays correctly at the prim level. HdXformSchema::Builder().Build()
    // returns the INNER container (matrix + resetXformStack), but the
    // prim-level overlay needs it keyed as "xform".
    HdContainerDataSourceHandle xformContainer =
        HdXformSchema::Builder()
            .SetMatrix(
                _ExecMatrixDataSource::New(
                    _execSystem.get(), prim, activeToken))
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(_resetXformStack))
            .Build();

    return HdRetainedContainerDataSource::New(
        HdXformSchemaTokens->xform, xformContainer);
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
            _currentTimeFrame += 1.0;
            UsdTimeCode newTime(_currentTimeFrame);
            _execSystem->ChangeTime(newTime);
            _currentTime = newTime;
        }
    }

    _SendPrimsDirtied(entries);
}

PXR_NAMESPACE_CLOSE_SCOPE

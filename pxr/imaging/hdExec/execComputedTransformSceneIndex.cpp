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

#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

// ---------------------------------------------------------------------------
// Schema metadata cache — loaded once from plugInfo.json across all plugins.
// Stores which schemas have exec computations and their per-schema settings.
// ---------------------------------------------------------------------------

namespace {

struct _ExecSchemaEntry {
    TfToken className;       // C++ class name from plugInfo (e.g. "UsdPhysicsRigidBodyAPI")
    TfToken identifier;      // Short schema identifier for HasAPI (e.g. "PhysicsRigidBodyAPI")
    bool resetXformStack = false;  // Per-schema: world-space (true) or local (false)
};

static std::mutex _sSchemaRegistryMutex;
static std::vector<_ExecSchemaEntry> _sExecSchemas;
static bool _sSchemasLoaded = false;

/// Lazily load schema metadata from all plugins' plugInfo.json.
/// Thread-safe; only loads once.
static const std::vector<_ExecSchemaEntry> &
_GetExecSchemas()
{
    std::lock_guard<std::mutex> lock(_sSchemaRegistryMutex);
    if (_sSchemasLoaded) {
        return _sExecSchemas;
    }

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
        for (const auto &entry : schemasIt->second.GetJsObject()) {
            if (!entry.second.IsObject()) {
                continue;
            }
            const JsObject &schemaObj = entry.second.GetJsObject();

            auto apcIt = schemaObj.find("allowsPluginComputations");
            if (apcIt == schemaObj.end() ||
                !apcIt->second.IsBool() ||
                !apcIt->second.GetBool()) {
                continue;
            }

            _ExecSchemaEntry se;
            se.className = TfToken(entry.first);

            // Resolve C++ class name → short schema identifier.
            const TfType &schemaType =
                TfType::FindByName(se.className.GetString());
            if (!schemaType.IsUnknown()) {
                const UsdSchemaRegistry::SchemaInfo *info =
                    UsdSchemaRegistry::FindSchemaInfo(schemaType);
                if (info) {
                    se.identifier = info->identifier;
                }
            }
            if (se.identifier.IsEmpty()) {
                continue;
            }

            // Per-schema resetXformStack (default: false).
            auto rxsIt = schemaObj.find("resetXformStack");
            if (rxsIt != schemaObj.end() && rxsIt->second.IsBool()) {
                se.resetXformStack = rxsIt->second.GetBool();
            }

            _sExecSchemas.push_back(std::move(se));
        }
    }
    _sSchemasLoaded = true;
    return _sExecSchemas;
}

/// Look up the resetXformStack setting for a prim based on its applied
/// schemas. Returns the value from the first matching schema entry, or
/// the filter's default if no schema-specific setting is found.
static bool
_GetResetXformStackForPrim(const UsdPrim &prim, bool filterDefault)
{
    const auto &schemas = _GetExecSchemas();
    for (const auto &se : schemas) {
        if (prim.HasAPI(se.identifier)) {
            return se.resetXformStack;
        }
    }
    return filterDefault;
}

} // anonymous namespace

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

// Static transform cache — written by Python on the main thread,
// read by TBB workers without the GIL.  Uses shared_ptr swap for
// lock-free reads on the hot path.
using _TransformMap = TfHashMap<SdfPath, GfMatrix4d, SdfPath::Hash>;
static std::mutex _sCacheMutex;
static std::shared_ptr<const _TransformMap> _sCachedTransforms =
    std::make_shared<const _TransformMap>();

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

std::optional<GfMatrix4d>
HdExecComputedTransformSceneIndex::QueryTransformProviders(
    const SdfPath &primPath, double timeSeconds)
{
    std::lock_guard<std::mutex> lock(_sTransformProviderMutex);
    for (const auto &entry : _sTransformProviders) {
        std::optional<GfMatrix4d> result = entry.second(primPath, timeSeconds);
        if (result) {
            return result;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Static Transform Cache
// ---------------------------------------------------------------------------

void
HdExecComputedTransformSceneIndex::SetCachedTransform(
    const SdfPath &primPath, const GfMatrix4d &matrix)
{
    std::lock_guard<std::mutex> lock(_sCacheMutex);
    auto newMap = std::make_shared<_TransformMap>(*_sCachedTransforms);
    (*newMap)[primPath] = matrix;
    _sCachedTransforms = std::move(newMap);
}

void
HdExecComputedTransformSceneIndex::ClearCachedTransform(
    const SdfPath &primPath)
{
    std::lock_guard<std::mutex> lock(_sCacheMutex);
    auto newMap = std::make_shared<_TransformMap>(*_sCachedTransforms);
    newMap->erase(primPath);
    _sCachedTransforms = std::move(newMap);
}

void
HdExecComputedTransformSceneIndex::ClearAllCachedTransforms()
{
    std::lock_guard<std::mutex> lock(_sCacheMutex);
    _sCachedTransforms = std::make_shared<const _TransformMap>();
}

std::optional<GfMatrix4d>
HdExecComputedTransformSceneIndex::GetCachedTransform(
    const SdfPath &primPath)
{
    // Lock-free read: grab shared_ptr, then look up.
    // The shared_ptr copy is atomic on most platforms; the underlying
    // map is immutable once published.
    std::shared_ptr<const _TransformMap> cache;
    {
        std::lock_guard<std::mutex> lock(_sCacheMutex);
        cache = _sCachedTransforms;
    }
    auto it = cache->find(primPath);
    if (it != cache->end()) {
        return it->second;
    }
    return std::nullopt;
}

void
HdExecComputedTransformSceneIndex::SetCachedTransforms(
    const std::vector<std::pair<SdfPath, GfMatrix4d>> &transforms)
{
    auto newMap = std::make_shared<_TransformMap>();
    newMap->reserve(transforms.size());
    for (const auto &pair : transforms) {
        (*newMap)[pair.first] = pair.second;
    }
    std::lock_guard<std::mutex> lock(_sCacheMutex);
    _sCachedTransforms = std::move(newMap);
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

    // Collect prim paths that need dirtying: any prim that has an exec
    // computation OR has a cached transform from a physics engine.
    std::shared_ptr<const _TransformMap> cachedTransforms;
    {
        std::lock_guard<std::mutex> cacheLock(_sCacheMutex);
        cachedTransforms = _sCachedTransforms;
    }

    for (auto *inst : _sInstances) {
        if (!inst->_bootstrapped) {
            continue;
        }

        if (inst->_execSystem) {
            inst->_execSystem->ChangeTime(time);
            inst->_currentTime = time;
            inst->_currentTimeFrame = time.IsDefault() ? 0.0 : time.GetValue();
        }

        HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;

        // Dirty prims with exec computations.
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

        // Dirty prims with cached transforms (from Python physics).
        // Also dirty their descendant renderables so the ancestor walk
        // in GetPrim() recomposes their world transforms.
        if (cachedTransforms) {
            for (const auto &pair : *cachedTransforms) {
                dirtyEntries.push_back({
                    pair.first,
                    HdDataSourceLocatorSet::UniversalSet()});

                // Walk descendants recursively.
                std::vector<SdfPath> stack;
                for (const auto &child :
                         inst->GetChildPrimPaths(pair.first)) {
                    stack.push_back(child);
                }
                while (!stack.empty()) {
                    SdfPath path = stack.back();
                    stack.pop_back();
                    dirtyEntries.push_back({
                        path,
                        HdDataSourceLocatorSet::UniversalSet()});
                    for (const auto &child :
                             inst->GetChildPrimPaths(path)) {
                        stack.push_back(child);
                    }
                }
            }
        }

        if (!dirtyEntries.empty()) {
            inst->_SendPrimsDirtied(dirtyEntries);
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
        // 1. Check the static transform cache first (no GIL needed).
        //    Python physics engines write here from the main thread.
        {
            std::optional<GfMatrix4d> cached =
                HdExecComputedTransformSceneIndex::GetCachedTransform(
                    _primPath);
            if (cached) {
                return *cached;
            }
        }

        // 2. Query registered transform providers (e.g. C++ physics).
        {
            double frame =
                HdExecComputedTransformSceneIndex::GetGlobalTimeFrame();
            double fps = 60.0;
            if (_stage) {
                fps = _stage->GetTimeCodesPerSecond();
                if (fps <= 0) fps = 60.0;
            }
            double timeSeconds = frame / fps;

            std::optional<GfMatrix4d> mat =
                HdExecComputedTransformSceneIndex::QueryTransformProviders(
                    _primPath, timeSeconds);
            if (mat) {
                return *mat;
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
    // Explicit-mode instances are immediately bootstrapped since the
    // caller provides stage + exec system.  Auto-bootstrap instances
    // set this flag lazily in _TryBootstrap().
    if (_stage && _execSystem) {
        _bootstrapped = true;
    }

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
    } else {
        // Even without an exec computation, check the static transform
        // cache and then TransformProvider callbacks.  The cache is
        // the preferred path for Python physics engines — it avoids
        // calling into Python from TBB threads entirely.
        std::optional<GfMatrix4d> providerResult =
            GetCachedTransform(primPath);

        if (!providerResult) {
            double frame = _sGlobalTimeFrame.load();
            double fps = 60.0;
            if (_stage) {
                fps = _stage->GetTimeCodesPerSecond();
                if (fps <= 0) fps = 60.0;
            }
            providerResult = QueryTransformProviders(primPath, frame / fps);
        }

        if (providerResult) {
            // Create a simple xform overlay with the provider's matrix.
            //
            // Physics engines (e.g. Newton GPU) supply WORLD-SPACE body
            // transforms (M_sim).  Use resetXformStack=true so that
            // Hydra's flattening treats the matrix as fully composed and
            // does NOT concatenate it with the parent's world xform —
            // which would double-apply the parent transform and cause
            // the "two cubes" bug (original + simulated position).
            //
            // When the prim has physics API metadata, honour the
            // per-schema resetXformStack setting from plugInfo.json.
            // Otherwise fall back to the filter's own default which is
            // set by the scene-index plugin (typically true).
            bool resetXformStack = _resetXformStack;
            if (_stage) {
                UsdPrim prim = _stage->GetPrimAtPath(primPath);
                if (prim) {
                    resetXformStack =
                        _GetResetXformStackForPrim(prim, _resetXformStack);
                }
            }
            HdContainerDataSourceHandle xformContainer =
                HdXformSchema::Builder()
                    .SetMatrix(
                        HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                            *providerResult))
                    .SetResetXformStack(
                        HdRetainedTypedSampledDataSource<bool>::New(
                            resetXformStack))
                    .Build();
            prim.dataSource = HdOverlayContainerDataSource::New(
                HdRetainedContainerDataSource::New(
                    HdXformSchemaTokens->xform, xformContainer),
                prim.dataSource);
        } else {
            // No direct cached transform or provider for this prim.
            // Check if any ANCESTOR has a cached physics transform.
            // If so, recompose this prim's world transform:
            //   M_new = M_input × inv(M_ancestor_input) × M_sim
            // This undoes the ancestor's original flattened transform
            // and applies the new physics transform.
            //
            // Only applies when the input provides pre-flattened world
            // transforms (resetXformStack=true), which is the case when
            // HdFlatteningSceneIndex is upstream of this filter.
            HdXformSchema inputXform =
                HdXformSchema::GetFromParent(prim.dataSource);
            bool inputIsFlattened = false;
            if (inputXform.IsDefined() && inputXform.GetResetXformStack()) {
                inputIsFlattened =
                    inputXform.GetResetXformStack()->GetTypedValue(0);
            }

            if (inputIsFlattened) {
                for (SdfPath ancestorPath = primPath.GetParentPath();
                     !ancestorPath.IsEmpty() &&
                         ancestorPath != SdfPath::AbsoluteRootPath();
                     ancestorPath = ancestorPath.GetParentPath()) {

                    std::optional<GfMatrix4d> ancestorSim =
                        GetCachedTransform(ancestorPath);
                    if (!ancestorSim) {
                        continue;
                    }

                    // Found an ancestor with a cached physics transform.
                    // Get the ancestor's ORIGINAL (pre-physics) flattened
                    // transform from the input scene index.
                    HdSceneIndexPrim ancestorPrim =
                        _GetInputSceneIndex()->GetPrim(ancestorPath);
                    HdXformSchema ancestorXform =
                        HdXformSchema::GetFromParent(ancestorPrim.dataSource);
                    if (!ancestorXform.IsDefined() ||
                        !ancestorXform.GetMatrix()) {
                        break;
                    }
                    GfMatrix4d ancestorOriginal =
                        ancestorXform.GetMatrix()->GetTypedValue(0);

                    // Guard: non-invertible ancestor transform (e.g.
                    // zero scale) would produce NaN.  Skip the walk
                    // and let the child keep its stale input xform —
                    // a frame of visual lag is better than NaN.
                    double det = ancestorOriginal.GetDeterminant();
                    if (GfIsClose(det, 0.0, 1e-12)) {
                        TF_WARN("HdExec: ancestor %s has non-invertible "
                                "transform (det=%.2e), skipping ancestor "
                                "walk for child %s",
                                ancestorPath.GetText(), det,
                                primPath.GetText());
                        break;
                    }

                    // Get this prim's current (stale) flattened transform.
                    GfMatrix4d childOriginal =
                        inputXform.GetMatrix()->GetTypedValue(0);

                    // Recompose:
                    // M_new = M_child × inv(M_ancestor_old) × M_sim
                    GfMatrix4d ancestorInv = ancestorOriginal.GetInverse();
                    GfMatrix4d childNew =
                        childOriginal * ancestorInv * (*ancestorSim);

                    HdContainerDataSourceHandle xformContainer =
                        HdXformSchema::Builder()
                            .SetMatrix(
                                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                                    childNew))
                            .SetResetXformStack(
                                HdRetainedTypedSampledDataSource<bool>::New(
                                    true))
                            .Build();
                    prim.dataSource = HdOverlayContainerDataSource::New(
                        HdRetainedContainerDataSource::New(
                            HdXformSchemaTokens->xform, xformContainer),
                        prim.dataSource);
                    break;
                }
            }
        }
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
    // for exec computations via plugInfo metadata.
    {
        const auto &schemas = _GetExecSchemas();

        bool hasSchema = false;
        for (const auto &se : schemas) {
            if (prim.HasAPI(se.identifier)) {
                hasSchema = true;
                break;
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

    // Determine resetXformStack from per-schema plugInfo metadata.
    // Physics and DSO want true (world-space), units wants false (local).
    bool resetXformStack =
        _GetResetXformStackForPrim(prim, _resetXformStack);

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
                HdRetainedTypedSampledDataSource<bool>::New(resetXformStack))
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

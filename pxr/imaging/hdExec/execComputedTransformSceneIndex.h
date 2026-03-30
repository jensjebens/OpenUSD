//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_EXEC_EXEC_COMPUTED_TRANSFORM_SCENE_INDEX_H
#define PXR_IMAGING_HD_EXEC_EXEC_COMPUTED_TRANSFORM_SCENE_INDEX_H

/// \file

#include "pxr/pxr.h"
#include "pxr/imaging/hdExec/api.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"

#include "pxr/base/tf/hashmap.h"
#include "pxr/base/tf/notice.h"
#include "pxr/base/tf/token.h"
// TfWeakBase is inherited via HdSceneIndexBase
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class ExecUsdSystem;

TF_DECLARE_REF_PTRS(HdExecComputedTransformSceneIndex);

///
/// \class HdExecComputedTransformSceneIndex
///
/// A scene index filter that overlays HdXformSchema on prims that have
/// registered OpenExec transform computations. This provides a generic bridge
/// between OpenExec computed values and the Hydra 2.0 rendering pipeline.
///
/// Any computation registered via EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA that
/// outputs GfMatrix4d can be consumed by this filter. The filter checks each
/// prim for the specified computation tokens and, if found, overlays the
/// exec-computed transform onto the prim's xform data source.
///
/// The filter supports two modes:
///   1. Explicit setup: New() with stage + exec system provided by the app.
///   2. Auto-bootstrap: NewAutoBootstrap() for scene-index-plugin use.
///      In this mode the filter lazily detects a UsdStage via
///      UsdNotice::StageContentsChanged and creates its own ExecUsdSystem.
///
class HdExecComputedTransformSceneIndex
    : public HdSingleInputFilteringSceneIndexBase
{
public:

    /// Creates a new HdExecComputedTransformSceneIndex with explicit setup.
    ///
    /// \param inputSceneIndex  The upstream scene index.
    /// \param stage            The USD stage used to look up prims for exec
    ///                         computation queries.
    /// \param execSystem       Shared pointer to the exec system that evaluates
    ///                         computations.
    /// \param computationTokens  Vector of computation names to look for
    ///                         (e.g., "computeLocalToWorldTransform",
    ///                         "computeSimulatedTransform").
    /// \param resetXformStack  Whether computed transforms are world-space
    ///                         (true) or local-space (false).
    ///
    HDEXEC_API
    static HdExecComputedTransformSceneIndexRefPtr
    New(const HdSceneIndexBaseRefPtr &inputSceneIndex,
        const UsdStageConstRefPtr &stage,
        std::shared_ptr<ExecUsdSystem> execSystem,
        const TfTokenVector &computationTokens,
        bool resetXformStack = false);

    /// Creates an auto-bootstrapping scene index filter.
    ///
    /// No stage or exec system is required at construction time. The filter
    /// lazily discovers a UsdStage via UsdNotice::StageContentsChanged and
    /// creates its own ExecUsdSystem when a stage with physics prims is
    /// detected.
    ///
    /// \param inputSceneIndex    The upstream scene index.
    /// \param computationTokens  Computation names to look for.
    /// \param resetXformStack    Whether computed transforms are world-space.
    ///
    HDEXEC_API
    static HdExecComputedTransformSceneIndexRefPtr
    NewAutoBootstrap(const HdSceneIndexBaseRefPtr &inputSceneIndex,
                     const TfTokenVector &computationTokens,
                     bool resetXformStack = false);

    /// Set the global stage for auto-bootstrapping scene indices.
    ///
    /// This is a static setter that allows external code (e.g., a notice
    /// handler or UsdImaging integration point) to provide a stage reference
    /// without requiring a direct dependency. Auto-bootstrapping instances
    /// query this on first GetPrim().
    ///
    HDEXEC_API
    static void SetGlobalStage(const UsdStageRefPtr &stage);

    /// Get the global stage (for computation callbacks that need it).
    HDEXEC_API
    static UsdStageRefPtr GetGlobalStage();

    /// Get the current global time frame.
    HDEXEC_API
    static double GetGlobalTimeFrame();

    /// Advance time for all auto-bootstrapped instances. Called by the
    /// imaging engine on each frame to ensure exec computations re-evaluate.
    HDEXEC_API
    static void AdvanceGlobalTime(UsdTimeCode time);

    /// \name Transform Providers
    /// @{
    ///
    /// Plugins can register named callbacks to provide transforms
    /// directly, bypassing exec's computation cache. This is needed
    /// because exec caches computations whose declared inputs don't
    /// change — but physics and DSO transforms change every frame as
    /// a side effect of stepping.
    ///
    /// Each provider receives a prim path and time in seconds.
    /// Return `std::nullopt` if the provider does not own the prim.
    /// Return a `GfMatrix4d` to claim the prim's transform.
    /// Providers are consulted in registration order; the first to
    /// return a value wins.
    ///
    using TransformProviderFn =
        std::function<std::optional<GfMatrix4d>(
            const SdfPath &primPath, double timeSeconds)>;

    /// Register a named transform provider.
    HDEXEC_API
    static void RegisterTransformProvider(
        const TfToken &name, TransformProviderFn fn);

    /// Remove a named transform provider.
    HDEXEC_API
    static void UnregisterTransformProvider(const TfToken &name);

    /// Query a transform from all registered providers.
    /// Returns the first non-nullopt result, or nullopt if no
    /// provider claims the prim.
    HDEXEC_API
    static std::optional<GfMatrix4d> QueryTransformProviders(
        const SdfPath &primPath, double timeSeconds);

    /// \name Static Transform Cache
    /// @{
    ///
    /// A thread-safe cache for pre-computed transforms. Python physics
    /// engines write transforms here from the main thread (with the GIL
    /// held), then Hydra's TBB workers read without needing the GIL.
    /// This avoids the GIL-from-TBB-thread problem entirely.
    ///

    /// Store a transform for a prim path. Thread-safe.
    HDEXEC_API
    static void SetCachedTransform(
        const SdfPath &primPath, const GfMatrix4d &matrix);

    /// Remove a cached transform. Thread-safe.
    HDEXEC_API
    static void ClearCachedTransform(const SdfPath &primPath);

    /// Clear all cached transforms. Thread-safe.
    HDEXEC_API
    static void ClearAllCachedTransforms();

    /// Query a cached transform. Returns nullopt if not cached.
    /// Thread-safe (lock-free read path via shared_ptr swap).
    HDEXEC_API
    static std::optional<GfMatrix4d> GetCachedTransform(
        const SdfPath &primPath);

    /// Batch-set transforms. Atomically replaces the entire cache.
    /// This is the preferred API for physics engines that update all
    /// body transforms each frame.
    HDEXEC_API
    static void SetCachedTransforms(
        const std::vector<std::pair<SdfPath, GfMatrix4d>> &transforms);

    /// @}

    HDEXEC_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    HDEXEC_API
    ~HdExecComputedTransformSceneIndex() override;

    HDEXEC_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

    /// Advance the exec system to a new time and dirty affected prims.
    HDEXEC_API
    void SetTime(UsdTimeCode time);

    /// Get the set of prim paths that have exec computations.
    HDEXEC_API
    SdfPathVector GetComputedPrimPaths() const;

protected:
    HdExecComputedTransformSceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex,
        const UsdStageConstRefPtr &stage,
        std::shared_ptr<ExecUsdSystem> execSystem,
        const TfTokenVector &computationTokens,
        bool resetXformStack);

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
    // --- Explicit mode members ---
    mutable UsdStageConstRefPtr _stage;
    mutable std::shared_ptr<ExecUsdSystem> _execSystem;
    TfTokenVector _computationTokens;
    bool _resetXformStack;

    // --- Auto-bootstrap members ---
    bool _autoBootstrap = false;
    mutable bool _bootstrapped = false;
    mutable UsdTimeCode _currentTime;
    mutable double _currentTimeFrame = 0.0;

    // Lazy bootstrap: try to acquire stage and create exec system.
    void _TryBootstrap() const;

    // Static global stage for auto-bootstrapping.
    static std::mutex _sGlobalStageMutex;
    static UsdStageRefPtr _sGlobalStage;

    // TfNotice listener key for stage contents changes.
    TfNotice::Key _stageNoticeKey;

    // Handler for UsdNotice::StageContentsChanged (global listener).
    void _OnStageContentsChanged(
        const UsdNotice::StageContentsChanged &notice);

    // --- Computation cache ---
    mutable std::mutex _cacheMutex;
    mutable TfHashMap<SdfPath, bool, SdfPath::Hash> _hasComputationCache;

    bool _HasExecComputation(const SdfPath &primPath) const;

    HdContainerDataSourceHandle _CreateExecXformDataSource(
        const SdfPath &primPath) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_EXEC_EXEC_COMPUTED_TRANSFORM_SCENE_INDEX_H

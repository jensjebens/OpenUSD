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
#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <memory>
#include <mutex>
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
class HdExecComputedTransformSceneIndex
    : public HdSingleInputFilteringSceneIndexBase
{
public:

    /// Creates a new HdExecComputedTransformSceneIndex.
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

    HDEXEC_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

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
    UsdStageConstRefPtr _stage;
    std::shared_ptr<ExecUsdSystem> _execSystem;
    TfTokenVector _computationTokens;
    bool _resetXformStack;

    // Cache of which prims have computations.
    mutable std::mutex _cacheMutex;
    mutable TfHashMap<SdfPath, bool, SdfPath::Hash> _hasComputationCache;

    bool _HasExecComputation(const SdfPath &primPath) const;

    HdContainerDataSourceHandle _CreateExecXformDataSource(
        const SdfPath &primPath) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_EXEC_EXEC_COMPUTED_TRANSFORM_SCENE_INDEX_H

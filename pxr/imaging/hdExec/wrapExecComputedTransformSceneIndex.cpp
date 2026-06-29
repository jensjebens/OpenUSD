//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file hdExec/wrapExecComputedTransformSceneIndex.cpp
/// \brief Python bindings for HdExecComputedTransformSceneIndex static API.
///
/// Exposes RegisterTransformProvider, SetGlobalStage, AdvanceGlobalTime
/// to Python so that Python-based physics engines (e.g. Newton GPU) can
/// participate in the Hydra transform pipeline.

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include "pxr/external/boost/python.hpp"
#include "pxr/external/boost/python/def.hpp"

#include <optional>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

// Wrap the C++ std::function<std::optional<GfMatrix4d>(...)> callback
// to accept a Python callable.
void
_RegisterTransformProvider(
    const TfToken &name,
    object pyCallable)
{
    // Copy the Python callable into a shared_ptr so the lambda captures
    // a ref-counted handle that survives past this scope.
    auto callable = std::make_shared<object>(pyCallable);

    HdExecComputedTransformSceneIndex::RegisterTransformProvider(
        name,
        [callable](const SdfPath &primPath,
                   double timeSeconds) -> std::optional<GfMatrix4d> {
            // Acquire the GIL for the Python callback.
            PyGILState_STATE gstate = PyGILState_Ensure();
            try {
                object result = (*callable)(primPath, timeSeconds);
                if (result.is_none()) {
                    PyGILState_Release(gstate);
                    return std::nullopt;
                }
                GfMatrix4d mat = extract<GfMatrix4d>(result);
                PyGILState_Release(gstate);
                return mat;
            } catch (error_already_set &) {
                PyErr_Print();
                PyGILState_Release(gstate);
                return std::nullopt;
            }
        });
}

void
_SetGlobalStage(const UsdStageRefPtr &stage)
{
    HdExecComputedTransformSceneIndex::SetGlobalStage(stage);
}

UsdStageRefPtr
_GetGlobalStage()
{
    return HdExecComputedTransformSceneIndex::GetGlobalStage();
}

double
_GetGlobalTimeFrame()
{
    return HdExecComputedTransformSceneIndex::GetGlobalTimeFrame();
}

void
_AdvanceGlobalTime(double timeCode)
{
    HdExecComputedTransformSceneIndex::AdvanceGlobalTime(
        UsdTimeCode(timeCode));
}

void
_UnregisterTransformProvider(const TfToken &name)
{
    HdExecComputedTransformSceneIndex::UnregisterTransformProvider(name);
}

void
_SetCachedTransform(const SdfPath &primPath, const GfMatrix4d &matrix)
{
    HdExecComputedTransformSceneIndex::SetCachedTransform(primPath, matrix);
}

void
_ClearCachedTransform(const SdfPath &primPath)
{
    HdExecComputedTransformSceneIndex::ClearCachedTransform(primPath);
}

void
_ClearAllCachedTransforms()
{
    HdExecComputedTransformSceneIndex::ClearAllCachedTransforms();
}

void
_SetCachedTransforms(const list &pyTransforms)
{
    std::vector<std::pair<SdfPath, GfMatrix4d>> transforms;
    for (int i = 0; i < len(pyTransforms); ++i) {
        tuple t = extract<tuple>(pyTransforms[i]);
        SdfPath path = extract<SdfPath>(t[0]);
        GfMatrix4d mat = extract<GfMatrix4d>(t[1]);
        transforms.emplace_back(std::move(path), mat);
    }
    HdExecComputedTransformSceneIndex::SetCachedTransforms(transforms);
}

} // anonymous namespace

void wrapExecComputedTransformSceneIndex()
{
    def("RegisterTransformProvider", &_RegisterTransformProvider,
        (arg("name"), arg("callback")),
        "Register a named transform provider callback.\n"
        "The callback signature is:\n"
        "  def provider(primPath: Sdf.Path, timeSeconds: float) "
        "-> Optional[Gf.Matrix4d]\n"
        "Return None if the provider does not own the prim.");

    def("UnregisterTransformProvider", &_UnregisterTransformProvider,
        (arg("name")),
        "Remove a named transform provider.");

    def("SetGlobalStage", &_SetGlobalStage,
        (arg("stage")),
        "Set the global stage for auto-bootstrapping.");

    def("GetGlobalStage", &_GetGlobalStage,
        "Get the global stage.");

    def("GetGlobalTimeFrame", &_GetGlobalTimeFrame,
        "Get the current global time frame.");

    def("AdvanceGlobalTime", &_AdvanceGlobalTime,
        (arg("timeCode")),
        "Advance time for all auto-bootstrapped instances.");

    def("SetCachedTransform", &_SetCachedTransform,
        (arg("primPath"), arg("matrix")),
        "Store a transform for a prim path in the static cache.\n"
        "Thread-safe. Use from the main thread before triggering a render.");

    def("ClearCachedTransform", &_ClearCachedTransform,
        (arg("primPath")),
        "Remove a cached transform.");

    def("ClearAllCachedTransforms", &_ClearAllCachedTransforms,
        "Clear all cached transforms.");

    def("SetCachedTransforms", &_SetCachedTransforms,
        (arg("transforms")),
        "Batch-set transforms. Takes a list of (Sdf.Path, Gf.Matrix4d) tuples.\n"
        "Atomically replaces the entire cache. Preferred for physics engines\n"
        "that update all body transforms each frame.");
}

//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonPhysicsComputations.cpp
/// \brief OpenExec computation and TransformProvider for Newton physics.
///
/// Registers `computeSimulatedTransform` for UsdPhysicsRigidBodyAPI and
/// a TransformProvider callback that HdExecComputedTransformSceneIndex
/// uses to query simulated transforms each frame (bypassing exec's cache).

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"
#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"

#include "newtonPhysicsSystem.h"

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeSimulatedTransform)
);

// ---------------------------------------------------------------------------
// TransformProvider: registered at plugin load time. Called by
// _ExecMatrixDataSource::GetTypedValue() each frame, bypassing exec's
// computation cache.
// ---------------------------------------------------------------------------

namespace {
struct _TransformProviderRegistrar {
    _TransformProviderRegistrar() {
        HdExecComputedTransformSceneIndex::RegisterTransformProvider(
            TfToken("newtonPhysics"),
            [](const SdfPath &primPath, double timeSeconds) -> GfMatrix4d {
                NewtonPhysicsSystem &sys =
                    NewtonPhysicsSystem::GetInstance();

                if (!sys.IsInitialized()) {
                    UsdStageRefPtr stage =
                        HdExecComputedTransformSceneIndex::GetGlobalStage();
                    if (stage) {
                        sys.EnsureInitialized(stage);
                    } else {
                        return GfMatrix4d(1.0);
                    }
                }

                sys.AdvanceToTime(timeSeconds);
                return sys.GetSimulatedTransform(primPath);
            });
    }
};
static _TransformProviderRegistrar _sRegistrar;
} // anonymous namespace

// ---------------------------------------------------------------------------
// OpenExec computation (used by the programmatic API; the render path
// goes through TransformProvider instead).
// ---------------------------------------------------------------------------

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)
{
    self.PrimComputation(_tokens->computeSimulatedTransform)
        .Callback<GfMatrix4d>(+[](const VdfContext &context) {
            const SdfPath primPath =
                context.GetInputValue<SdfPath>(
                    ExecBuiltinComputations->computePath);

            NewtonPhysicsSystem &sys =
                NewtonPhysicsSystem::GetInstance();

            if (!sys.IsInitialized()) {
                UsdStageRefPtr stage =
                    HdExecComputedTransformSceneIndex::GetGlobalStage();
                if (stage) {
                    sys.EnsureInitialized(stage);
                } else {
                    return GfMatrix4d(1.0);
                }
            }

            double frame =
                HdExecComputedTransformSceneIndex::GetGlobalTimeFrame();
            UsdStageRefPtr stage =
                HdExecComputedTransformSceneIndex::GetGlobalStage();
            double fps = stage ? stage->GetTimeCodesPerSecond() : 60.0;
            if (fps <= 0) fps = 60.0;
            sys.AdvanceToTime(frame / fps);

            return sys.GetSimulatedTransform(primPath);
        })
        .Inputs(
            Computation<SdfPath>(ExecBuiltinComputations->computePath)
        );
}

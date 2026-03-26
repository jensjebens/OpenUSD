//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonPhysicsComputations.cpp
/// \brief Registers OpenExec computations for UsdPhysicsRigidBodyAPI.
///
/// Defines a `computeSimulatedTransform` computation that queries the
/// NewtonPhysicsSystem singleton for the current simulated transform
/// of a rigid body prim. The computation uses the builtin `computePath`
/// to resolve which prim is being evaluated, then looks up the
/// transform directly from the physics system — no session layer
/// involved.
///
/// Pipeline:
///   Newton steps world → NewtonPhysicsSystem stores transforms
///   → This computation queries NewtonPhysicsSystem via prim path
///   → HdExecComputedTransformSceneIndex delivers to Hydra
///   → Storm renders

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"
#include "pxr/exec/ef/time.h"
#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"

#include "newtonPhysicsSystem.h"

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeSimulatedTransform)
);

// ---------------------------------------------------------------------------
// Register Newton as the transform provider for HdExec.
// This callback is invoked by _ExecMatrixDataSource::GetTypedValue() on
// every frame, bypassing exec's computation cache (which doesn't
// invalidate for side-effect-driven physics stepping).
// ---------------------------------------------------------------------------

namespace {
struct _TransformProviderRegistrar {
    _TransformProviderRegistrar() {
        HdExecComputedTransformSceneIndex::SetTransformProvider(
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
        fprintf(stderr, "[Newton] Transform provider registered with HdExec\n");
    }
};
static _TransformProviderRegistrar _sRegistrar;
} // anonymous namespace

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)
{
    self.PrimComputation(_tokens->computeSimulatedTransform)
        .Callback<GfMatrix4d>(+[](const VdfContext &context) {
            fprintf(stderr, "[Newton] COMPUTATION CALLBACK ENTERED\n");
            
            // Get this prim's path from the builtin computePath.
            const SdfPath primPath =
                context.GetInputValue<SdfPath>(
                    ExecBuiltinComputations->computePath);

            fprintf(stderr, "[Newton] prim=%s\n", primPath.GetText());

            // Query the Newton physics system for the simulated
            // transform. Lazy-init if needed.
            NewtonPhysicsSystem &sys =
                NewtonPhysicsSystem::GetInstance();
            
            if (!sys.IsInitialized()) {
                // Try to get the stage from the prim
                UsdPrim prim = UsdPrim();
                // Use the global stage from HdExec
                UsdStageRefPtr stage = 
                    HdExecComputedTransformSceneIndex::GetGlobalStage();
                if (stage) {
                    fprintf(stderr, "[Newton] Lazy-initializing physics from computation\n");
                    sys.EnsureInitialized(stage);
                } else {
                    fprintf(stderr, "[Newton] No stage available, returning identity\n");
                    return GfMatrix4d(1.0);
                }
            }
            
            // Step Newton to the current time
            double frame = HdExecComputedTransformSceneIndex::GetGlobalTimeFrame();
            double fps = 24.0; // TODO: read from stage metadata
            double timeInSeconds = frame / fps;
            sys.AdvanceToTime(timeInSeconds);

            GfMatrix4d result = sys.GetSimulatedTransform(primPath);
            GfVec3d pos = result.ExtractTranslation();
            fprintf(stderr, "[Newton] Result: translate=(%.2f, %.2f, %.2f)\n",
                    pos[0], pos[1], pos[2]);
            return result;
        })
        .Inputs(
            Computation<SdfPath>(ExecBuiltinComputations->computePath)
        );
}

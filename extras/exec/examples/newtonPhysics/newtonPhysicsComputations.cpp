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
#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"

#include "newtonPhysicsSystem.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeSimulatedTransform)
);

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)
{
    // Register a computation that outputs the simulated transform.
    //
    // The actual simulation is driven by NewtonPhysicsSystem which
    // stores per-body transforms after each Newton world step. This
    // computation reads the prim's path via the builtin computePath,
    // then queries the physics system directly — no session layer.
    //
    self.PrimComputation(_tokens->computeSimulatedTransform)
        .Callback<GfMatrix4d>(+[](const VdfContext &context) {
            const bool enabled =
                context.GetInputValue<bool>(
                    TfToken("physics:rigidBodyEnabled"));

            if (!enabled) {
                return GfMatrix4d(1.0);
            }

            // Get this prim's path from the builtin computePath.
            const SdfPath primPath =
                context.GetInputValue<SdfPath>(
                    ExecBuiltinComputations->computePath);

            // Query the Newton physics system for the simulated
            // transform. Returns identity if the prim is not mapped.
            NewtonPhysicsSystem &sys =
                NewtonPhysicsSystem::GetInstance();
            return sys.GetSimulatedTransform(primPath);
        })
        .Inputs(
            AttributeValue<bool>(TfToken("physics:rigidBodyEnabled")),
            Computation(ExecBuiltinComputations->computePath)
        );
}

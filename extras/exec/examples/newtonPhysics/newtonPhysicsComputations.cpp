//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonPhysicsComputations.cpp
/// \brief Registers OpenExec computations for UsdPhysicsRigidBodyAPI.
///
/// Defines a `computeSimulatedTransform` computation that reads back
/// the simulated transform for a rigid body. The actual simulation is
/// driven by NewtonSimulationDriver, which writes transforms to a
/// session sublayer. This computation reads those authored values,
/// providing an OpenExec-compatible interface into the simulation
/// results.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeSimulatedTransform)
);

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)
{
    // Register a computation that outputs the simulated transform.
    //
    // The actual simulation is driven by NewtonSimulationDriver which
    // writes results to a session sublayer. This computation reads back
    // those authored values, providing an OpenExec-compatible interface.
    //
    // When the driver has not yet written a value (or in stub mode),
    // the computation falls back to the initially authored
    // xformOp:translate, producing an identity-like transform.
    //
    self.PrimComputation(_tokens->computeSimulatedTransform)
        .Callback<GfMatrix4d>(+[](const VdfContext &context) {
            const bool enabled =
                context.GetInputValue<bool>(
                    TfToken("physics:rigidBodyEnabled"));

            if (!enabled) {
                return GfMatrix4d(1.0);
            }

            // Read back the simulated translate (authored by the
            // driver on the session layer). When not yet simulated,
            // falls back to the initially authored value from the
            // root layer.
            const GfVec3d translate =
                context.GetInputValue<GfVec3d>(
                    TfToken("xformOp:translate"));

            GfMatrix4d result(1.0);
            result.SetTranslateOnly(translate);
            return result;
        })
        .Inputs(
            AttributeValue<bool>(TfToken("physics:rigidBodyEnabled")),
            AttributeValue<GfVec3d>(TfToken("xformOp:translate"))
        );
}

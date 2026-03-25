//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonPhysicsComputations.cpp
/// \brief Registers OpenExec computations for UsdPhysicsRigidBodyAPI.
///
/// Phase 0: Stub registration — no computations defined yet.
/// Phase 3 will add computeSimulatedTransform and related computations.

#include "pxr/pxr.h"

#include "pxr/exec/exec/registerSchema.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"

PXR_NAMESPACE_USING_DIRECTIVE

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)
{
    // Phase 0: stub — no computations registered yet.
    //
    // Phase 3 will add:
    //   self.PrimComputation(_tokens->computeSimulatedTransform)
    //       .Callback<GfMatrix4d>(...)
    //       .Inputs(...)
}

//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/usdToNewtonMapper.h
/// \brief Maps USD physics prims to Newton Dynamics bodies.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_USD_TO_NEWTON_MAPPER_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_USD_TO_NEWTON_MAPPER_H

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"

#include <cstddef>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdToNewtonMapper
///
/// Traverses a USD stage and maps physics prims (those with
/// PhysicsRigidBodyAPI applied) to Newton Dynamics bodies.
/// In Phase 0 this is a stub — it counts physics bodies but
/// does not create actual Newton objects.
///
class UsdToNewtonMapper
{
public:
    UsdToNewtonMapper() = default;

    /// Traverse the stage, find all prims with PhysicsRigidBodyAPI,
    /// and map them to Newton bodies.
    /// Phase 0: counts physics prims and logs the result.
    void MapStage(const UsdStageRefPtr &stage);

    /// Returns the number of physics bodies found during the
    /// most recent MapStage() call.
    size_t GetBodyCount() const { return _bodyCount; }

private:
    size_t _bodyCount = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_USD_TO_NEWTON_MAPPER_H

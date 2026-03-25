//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonPhysicsSystem.h
/// \brief Central orchestrator tying the NewtonWorldManager and
///        UsdToNewtonMapper together with lazy initialization and
///        frame stepping.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_PHYSICS_SYSTEM_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_PHYSICS_SYSTEM_H

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include "usdToNewtonMapper.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class NewtonPhysicsSystem
///
/// Singleton that ties together the NewtonWorldManager and UsdToNewtonMapper.
/// Handles lazy initialization, frame stepping, and simulated transform
/// queries. The system initializes once per stage (or after Reset) and
/// tracks simulation time to avoid redundant stepping.
///
class NewtonPhysicsSystem
{
public:
    /// Returns the singleton instance.
    static NewtonPhysicsSystem &GetInstance();

    /// Ensure the system is initialized for the given stage.
    /// Idempotent — only initializes once per stage (or after Reset).
    void EnsureInitialized(const UsdStageRefPtr &stage);

    /// Advance simulation to the given time.
    /// Handles frame tracking — only steps if time has actually changed.
    void AdvanceToTime(double timeInSeconds);

    /// Get the simulated transform for a prim.
    GfMatrix4d GetSimulatedTransform(const SdfPath &path) const;

    /// Reset everything — world, mapper, time tracking.
    void Reset();

    /// Get the mapper (for body queries).
    const UsdToNewtonMapper &GetMapper() const { return _mapper; }

    /// Returns true if the system has been initialized.
    bool IsInitialized() const { return _initialized; }

private:
    NewtonPhysicsSystem() = default;
    ~NewtonPhysicsSystem() = default;

    NewtonPhysicsSystem(const NewtonPhysicsSystem &) = delete;
    NewtonPhysicsSystem &operator=(const NewtonPhysicsSystem &) = delete;

    UsdToNewtonMapper _mapper;
    double _currentSimTime = 0.0;
    double _lastStepTime = -1.0;
    bool _initialized = false;
    UsdStageRefPtr _cachedStage;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_PHYSICS_SYSTEM_H

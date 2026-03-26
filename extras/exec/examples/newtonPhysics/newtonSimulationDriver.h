//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonSimulationDriver.h
/// \brief DEPRECATED — Convenience wrapper around NewtonPhysicsSystem.
///
/// This class originally wrote simulated transforms to a session sublayer.
/// That approach has been replaced by the clean pipeline where OpenExec
/// computations query NewtonPhysicsSystem directly via computePath.
///
/// The driver is retained as a thin convenience wrapper around
/// NewtonPhysicsSystem for backward compatibility and as an alternative
/// integration path (e.g., for tests that don't use the full exec
/// pipeline).
///
/// Preferred pipeline (Phase 3):
///   NewtonPhysicsSystem::AdvanceToTime()
///   → OpenExec computeSimulatedTransform reads via computePath
///   → HdExecComputedTransformSceneIndex delivers to Hydra

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_SIMULATION_DRIVER_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_SIMULATION_DRIVER_H

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include "newtonPhysicsSystem.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class NewtonSimulationDriver
///
/// DEPRECATED — Thin convenience wrapper around NewtonPhysicsSystem.
///
/// Previously wrote simulated transforms to a USD session sublayer.
/// Now delegates entirely to NewtonPhysicsSystem. Use
/// NewtonPhysicsSystem directly for new code.
///
class NewtonSimulationDriver
{
public:
    NewtonSimulationDriver() = default;
    ~NewtonSimulationDriver() = default;

    /// Initialize the driver with a stage.
    /// Delegates to NewtonPhysicsSystem::EnsureInitialized().
    void Initialize(const UsdStageRefPtr &stage);

    /// Advance simulation by \p dt seconds.
    /// Delegates to NewtonPhysicsSystem::AdvanceToTime().
    void StepAndWriteBack(double dt);

    /// Advance simulation to a specific time code.
    void AdvanceToTimeCode(UsdTimeCode time, double timeCodesPerSecond);

    /// Reset the simulation.
    void Reset();

    /// Returns true if Initialize() has been called successfully.
    bool IsInitialized() const { return _initialized; }

    /// Get the current simulation time in seconds.
    double GetCurrentSimTime() const { return _currentSimTime; }

    /// Get the mapper (for body queries in tests).
    const UsdToNewtonMapper &GetMapper() const;

private:
    double _currentSimTime = 0.0;
    bool _initialized = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_SIMULATION_DRIVER_H

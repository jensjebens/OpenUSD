//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonSimulationDriver.h
/// \brief Session-layer simulation driver that steps Newton, reads back
///        simulated transforms, and authors them to a session sublayer.
///
/// This is the primary simulation integration path for the POC. The
/// driver creates an anonymous session sublayer, steps the Newton world
/// each frame, and writes back per-body xformOp:translate values. The
/// authored transforms are then visible to everything consuming the stage
/// (including USDView's Hydra renderer via stage change notices).

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_SIMULATION_DRIVER_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_SIMULATION_DRIVER_H

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include "usdToNewtonMapper.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class NewtonSimulationDriver
///
/// Steps the Newton physics world and writes simulated transforms back to
/// a USD session sublayer. This approach ensures that simulated positions
/// are visible to all stage consumers (Hydra, OpenExec, etc.) via normal
/// USD composition.
///
class NewtonSimulationDriver
{
public:
    NewtonSimulationDriver() = default;
    ~NewtonSimulationDriver() = default;

    /// Initialize the driver with a stage.
    /// Finds the PhysicsScene, initializes the world manager,
    /// maps the stage, and creates a session sublayer for physics results.
    void Initialize(const UsdStageRefPtr &stage);

    /// Advance simulation by \p dt seconds and write transforms to the
    /// session layer.
    void StepAndWriteBack(double dt);

    /// Advance simulation to a specific time code (handles frame timing).
    /// \p timeCodesPerSecond is the stage's time-codes-per-second value.
    void AdvanceToTimeCode(UsdTimeCode time, double timeCodesPerSecond);

    /// Reset the simulation — clears world, mapper, and session layer.
    void Reset();

    /// Returns true if Initialize() has been called successfully.
    bool IsInitialized() const { return _initialized; }

    /// Get the current simulation time in seconds.
    double GetCurrentSimTime() const { return _currentSimTime; }

    /// Get the mapper (for body queries in tests).
    const UsdToNewtonMapper &GetMapper() const { return _mapper; }

private:
    /// Write the simulated transforms of all dynamic bodies to the
    /// session layer.
    void _WriteTransformsToSessionLayer();

    UsdToNewtonMapper _mapper;
    SdfLayerRefPtr _sessionLayer;
    UsdStageRefPtr _stage;
    double _currentSimTime = 0.0;
    bool _initialized = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_SIMULATION_DRIVER_H

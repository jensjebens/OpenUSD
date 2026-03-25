//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonWorldManager.h
/// \brief Manages a Newton Dynamics ndWorld instance per USD stage.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_WORLD_MANAGER_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_WORLD_MANAGER_H

#include "pxr/pxr.h"
#include "pxr/base/gf/vec3d.h"

#ifdef NEWTON_DYNAMICS_FOUND
#include <ndNewton.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

class UsdPhysicsScene;

/// \class NewtonWorldManager
///
/// Singleton that manages the Newton Dynamics world state.
///
/// When Newton Dynamics is available (NEWTON_DYNAMICS_FOUND), this class
/// creates and manages an actual ndWorld instance. Gravity is stored by the
/// manager and applied per-body via ndBodyNotify during body creation
/// (Phase 2), since Newton 4 does not store gravity on the world itself.
///
/// When Newton is not available, the manager tracks gravity and
/// initialization state as a stub so tests still pass.
///
class NewtonWorldManager
{
public:
    /// Returns the singleton instance.
    static NewtonWorldManager &GetInstance();

    /// Initialize the Newton world from a UsdPhysicsScene.
    /// Reads gravity direction and magnitude from the scene.
    void Initialize(const UsdPhysicsScene &scene);

    /// Initialize with an explicit gravity vector (for testing).
    void Initialize(const GfVec3d &gravity);

    /// Step the physics simulation forward by \p dt seconds.
    /// Uses sub-stepping if dt > _maxSubStep.
    void Step(double dt);

    /// Destroy the Newton world and reset state.
    void Reset();

    /// Returns the currently configured gravity vector.
    GfVec3d GetGravity() const { return _gravity; }

    /// Returns true if Initialize() has been called.
    bool IsInitialized() const { return _initialized; }

    /// Returns the fixed simulation timestep.
    double GetTimestep() const { return _timestep; }

    /// Set the fixed simulation timestep.
    void SetTimestep(double dt) { _timestep = dt; }

    /// Returns accumulated simulation time.
    double GetAccumulatedTime() const { return _accumulatedTime; }

#ifdef NEWTON_DYNAMICS_FOUND
    /// Add a body to the Newton world. The world takes shared ownership.
    void AddBody(const ndSharedPtr<ndBody> &body);

    /// Get the ndWorld pointer (for advanced operations).
    ndWorld *GetWorld() { return _world; }
#endif

private:
    NewtonWorldManager() = default;
    ~NewtonWorldManager();

    NewtonWorldManager(const NewtonWorldManager &) = delete;
    NewtonWorldManager &operator=(const NewtonWorldManager &) = delete;

    GfVec3d _gravity = GfVec3d(0.0, -9.81, 0.0);
    double _timestep = 1.0 / 60.0;     // fixed timestep
    double _accumulatedTime = 0.0;
    double _maxSubStep = 1.0 / 30.0;   // max single sub-step
    bool _initialized = false;

#ifdef NEWTON_DYNAMICS_FOUND
    ndWorld* _world = nullptr;
#endif
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_WORLD_MANAGER_H

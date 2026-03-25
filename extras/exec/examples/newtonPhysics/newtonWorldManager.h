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

PXR_NAMESPACE_OPEN_SCOPE

class UsdPhysicsScene;

/// \class NewtonWorldManager
///
/// Singleton that manages the Newton Dynamics world state.
/// In Phase 0 this is a stub — it stores gravity and provides
/// no-op Step/Reset methods.
///
class NewtonWorldManager
{
public:
    /// Returns the singleton instance.
    static NewtonWorldManager &GetInstance();

    /// Initialize the Newton world from a UsdPhysicsScene.
    /// Reads gravity direction and magnitude.
    /// Phase 0: stores gravity, does not create an actual ndWorld.
    void Initialize(const UsdPhysicsScene &scene);

    /// Step the physics simulation forward by \p dt seconds.
    /// Phase 0: no-op.
    void Step(double dt);

    /// Destroy and recreate the Newton world.
    /// Phase 0: resets stored state.
    void Reset();

    /// Returns the currently configured gravity vector.
    GfVec3d GetGravity() const { return _gravity; }

    /// Returns true if Initialize() has been called.
    bool IsInitialized() const { return _initialized; }

private:
    NewtonWorldManager() = default;
    ~NewtonWorldManager() = default;

    NewtonWorldManager(const NewtonWorldManager &) = delete;
    NewtonWorldManager &operator=(const NewtonWorldManager &) = delete;

    GfVec3d _gravity = GfVec3d(0.0, -9.81, 0.0);
    bool _initialized = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_WORLD_MANAGER_H

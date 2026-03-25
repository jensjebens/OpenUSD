//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "newtonWorldManager.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usdPhysics/scene.h"

PXR_NAMESPACE_OPEN_SCOPE

NewtonWorldManager &
NewtonWorldManager::GetInstance()
{
    static NewtonWorldManager instance;
    return instance;
}

void
NewtonWorldManager::Initialize(const UsdPhysicsScene &scene)
{
    // Read gravity from the UsdPhysicsScene.
    GfVec3f gravityDirection;
    if (scene.GetGravityDirectionAttr().Get(&gravityDirection)) {
        float gravityMagnitude = 9.81f;
        scene.GetGravityMagnitudeAttr().Get(&gravityMagnitude);
        GfVec3d gravity(
            gravityDirection[0] * gravityMagnitude,
            gravityDirection[1] * gravityMagnitude,
            gravityDirection[2] * gravityMagnitude);
        Initialize(gravity);
    }
    else {
        // Default: Y-up gravity.
        Initialize(GfVec3d(0.0, -9.81, 0.0));
    }
}

void
NewtonWorldManager::Initialize(const GfVec3d &gravity)
{
    // Reset any existing world first.
    if (_initialized) {
        Reset();
    }

    _gravity = gravity;
    _accumulatedTime = 0.0;

#ifdef NEWTON_DYNAMICS_FOUND
    _world = new ndWorld();
    _world->SetSubSteps(2);
    // Note: Newton 4 does not store gravity on the world.
    // Gravity is applied per-body via ndBodyNotify::OnApplyExternalForce()
    // during body creation (Phase 2). We store the gravity vector in
    // _gravity so it can be queried and applied to bodies later.
#endif

    _initialized = true;

    TF_STATUS("NewtonWorldManager initialized with gravity (%f, %f, %f)",
              _gravity[0], _gravity[1], _gravity[2]);
}

void
NewtonWorldManager::Step(double dt)
{
    if (!_initialized) {
        TF_WARN("NewtonWorldManager::Step called before Initialize.");
        return;
    }

#ifdef NEWTON_DYNAMICS_FOUND
    // Newton 4 async stepping: Update() begins the simulation step,
    // Sync() blocks until it completes.
    _world->Update(static_cast<ndFloat32>(dt));
    _world->Sync();
#endif

    _accumulatedTime += dt;
}

void
NewtonWorldManager::Reset()
{
#ifdef NEWTON_DYNAMICS_FOUND
    if (_world) {
        delete _world;
        _world = nullptr;
    }
#endif

    _gravity = GfVec3d(0.0, -9.81, 0.0);
    _accumulatedTime = 0.0;
    _initialized = false;

    TF_STATUS("NewtonWorldManager reset.");
}

NewtonWorldManager::~NewtonWorldManager()
{
    Reset();
}

PXR_NAMESPACE_CLOSE_SCOPE

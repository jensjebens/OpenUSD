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
        _gravity = GfVec3d(
            gravityDirection[0] * gravityMagnitude,
            gravityDirection[1] * gravityMagnitude,
            gravityDirection[2] * gravityMagnitude);
    }
    else {
        // Default: Y-up gravity.
        _gravity = GfVec3d(0.0, -9.81, 0.0);
    }

    _initialized = true;

    TF_STATUS("NewtonWorldManager initialized with gravity (%f, %f, %f)",
              _gravity[0], _gravity[1], _gravity[2]);
}

void
NewtonWorldManager::Step(double dt)
{
    // Phase 0: no-op.
    // Phase 2 will call ndWorld::Update(dt) here.
    (void)dt;
}

void
NewtonWorldManager::Reset()
{
    _gravity = GfVec3d(0.0, -9.81, 0.0);
    _initialized = false;

    TF_STATUS("NewtonWorldManager reset.");
}

PXR_NAMESPACE_CLOSE_SCOPE

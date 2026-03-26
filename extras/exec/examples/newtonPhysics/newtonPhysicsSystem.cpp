//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "newtonPhysicsSystem.h"
#include "newtonWorldManager.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdPhysics/scene.h"

PXR_NAMESPACE_OPEN_SCOPE

NewtonPhysicsSystem &
NewtonPhysicsSystem::GetInstance()
{
    static NewtonPhysicsSystem instance;
    return instance;
}

void
NewtonPhysicsSystem::EnsureInitialized(const UsdStageRefPtr &stage)
{
    if (!stage) {
        TF_WARN("NewtonPhysicsSystem::EnsureInitialized called with "
                 "null stage.");
        return;
    }

    // If already initialized with the same stage, nothing to do.
    if (_initialized && _cachedStage == stage) {
        return;
    }

    // Reset any prior state.
    if (_initialized) {
        Reset();
    }

    // Find the PhysicsScene prim by traversal.
    UsdPhysicsScene physicsScene;
    for (const UsdPrim &prim : stage->Traverse()) {
        physicsScene = UsdPhysicsScene(prim);
        if (physicsScene) {
            break;
        }
    }

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();

    if (physicsScene) {
        mgr.Initialize(physicsScene);
    } else {
        // No PhysicsScene found — use default gravity.
        TF_WARN("No UsdPhysicsScene found on stage. "
                 "Using default gravity (0, -9.81, 0).");
        mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));
    }

    _mapper.MapStage(stage);
    _cachedStage = stage;
    _currentSimTime = 0.0;
    _lastStepTime = -1.0;
    _initialized = true;

    TF_STATUS("NewtonPhysicsSystem initialized with %zu bodies.",
              _mapper.GetBodyCount());
}

void
NewtonPhysicsSystem::AdvanceToTime(double timeInSeconds)
{
    if (!_initialized) {
        TF_WARN("NewtonPhysicsSystem::AdvanceToTime called before "
                 "initialization.");
        return;
    }

    // Already at this time — no-op.
    if (timeInSeconds == _lastStepTime) {
        return;
    }

    double dt = timeInSeconds - _currentSimTime;

    if (dt <= 0.0) {
        // Going backwards is not supported in this POC.
        TF_WARN("NewtonPhysicsSystem::AdvanceToTime: backwards time "
                 "step requested (current=%.4f, requested=%.4f). "
                 "Ignoring.",
                 _currentSimTime, timeInSeconds);
        return;
    }

    NewtonWorldManager::GetInstance().Step(dt);
    _mapper.UpdateSimulatedTransforms();  // Pull transforms from Newton
    _currentSimTime = timeInSeconds;
    _lastStepTime = timeInSeconds;
}

GfMatrix4d
NewtonPhysicsSystem::GetSimulatedTransform(const SdfPath &path) const
{
    return _mapper.GetSimulatedTransform(path);
}

void
NewtonPhysicsSystem::Reset()
{
    _mapper.Clear();
    NewtonWorldManager::GetInstance().Reset();
    _currentSimTime = 0.0;
    _lastStepTime = -1.0;
    _initialized = false;
    _cachedStage = nullptr;

    TF_STATUS("NewtonPhysicsSystem reset.");
}

PXR_NAMESPACE_CLOSE_SCOPE

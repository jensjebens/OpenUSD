//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonSimulationDriver.cpp
/// \brief DEPRECATED — Thin wrapper around NewtonPhysicsSystem.
///
/// All simulation logic now lives in NewtonPhysicsSystem. This driver
/// delegates to it for backward compatibility.

#include "newtonSimulationDriver.h"
#include "newtonPhysicsSystem.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE

void
NewtonSimulationDriver::Initialize(const UsdStageRefPtr &stage)
{
    if (!stage) {
        TF_WARN("NewtonSimulationDriver::Initialize called with "
                 "null stage.");
        return;
    }

    // Reset any prior state.
    if (_initialized) {
        Reset();
    }

    NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
    sys.EnsureInitialized(stage);

    _currentSimTime = 0.0;
    _initialized = true;

    TF_STATUS("NewtonSimulationDriver initialized (delegates to "
              "NewtonPhysicsSystem).");
}

void
NewtonSimulationDriver::StepAndWriteBack(double dt)
{
    if (!_initialized) {
        TF_WARN("NewtonSimulationDriver::StepAndWriteBack called "
                 "before initialization.");
        return;
    }

    if (dt <= 0.0) {
        return;
    }

    _currentSimTime += dt;
    NewtonPhysicsSystem::GetInstance().AdvanceToTime(_currentSimTime);
}

void
NewtonSimulationDriver::AdvanceToTimeCode(UsdTimeCode time,
                                           double timeCodesPerSecond)
{
    if (!_initialized) {
        TF_WARN("NewtonSimulationDriver::AdvanceToTimeCode called "
                 "before initialization.");
        return;
    }

    if (time.IsDefault() || timeCodesPerSecond <= 0.0) {
        return;
    }

    double targetSeconds = time.GetValue() / timeCodesPerSecond;
    if (targetSeconds > _currentSimTime) {
        _currentSimTime = targetSeconds;
        NewtonPhysicsSystem::GetInstance().AdvanceToTime(_currentSimTime);
    }
}

void
NewtonSimulationDriver::Reset()
{
    NewtonPhysicsSystem::GetInstance().Reset();
    _currentSimTime = 0.0;
    _initialized = false;

    TF_STATUS("NewtonSimulationDriver reset.");
}

const UsdToNewtonMapper &
NewtonSimulationDriver::GetMapper() const
{
    return NewtonPhysicsSystem::GetInstance().GetMapper();
}

PXR_NAMESPACE_CLOSE_SCOPE

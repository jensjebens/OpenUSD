//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonBodyNotify.h
/// \brief DEPRECATED — Use ndBodyNotify directly.
///
/// Newton Dynamics 4's base ndBodyNotify class already handles gravity
/// when constructed with a gravity vector:
///
///   body->SetNotifyCallback(new ndBodyNotify(
///       ndVector(0.0f, -9.81f, 0.0f, 0.0f)));
///
/// The base class's OnApplyExternalForce applies F = m * gravity
/// automatically. This custom subclass is no longer needed and is
/// kept only for backward compatibility.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_BODY_NOTIFY_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_BODY_NOTIFY_H

#ifdef NEWTON_DYNAMICS_FOUND

#include <ndNewton.h>

/// \class NewtonGravityNotify
///
/// DEPRECATED — Use ndBodyNotify(gravityVector) directly instead.
///
/// This class is kept for backward compatibility but is no longer used
/// by the mapper. The base ndBodyNotify constructor accepts a gravity
/// vector and handles external force application correctly.
///
class [[deprecated("Use ndBodyNotify(gravityVector) directly")]]
NewtonGravityNotify : public ndBodyNotify
{
public:
    /// Construct with a gravity vector (e.g., {0, -9.81, 0, 0}).
    NewtonGravityNotify(const ndVector &gravity)
        : ndBodyNotify(gravity)  // pass gravity to base — it handles the rest
    {
    }

    /// Called when the body's transform changes.
    /// Phase 3 will use this to flag dirty transforms for OpenExec
    /// writeback.
    void OnTransform(ndInt32 threadIndex,
                     const ndMatrix &matrix) override
    {
        // TODO(Phase 3): Flag prim as needing transform writeback.
    }
};

#endif // NEWTON_DYNAMICS_FOUND

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_BODY_NOTIFY_H

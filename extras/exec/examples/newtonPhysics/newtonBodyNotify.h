//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonBodyNotify.h
/// \brief Custom ndBodyNotify that applies gravity to Newton 4 bodies.
///
/// Newton Dynamics 4 applies external forces (including gravity) per-body
/// via the ndBodyNotify callback, rather than storing gravity on the world.
/// This class provides the gravity callback for dynamic rigid bodies.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_BODY_NOTIFY_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_BODY_NOTIFY_H

#ifdef NEWTON_DYNAMICS_FOUND

#include <ndNewton.h>

/// \class NewtonGravityNotify
///
/// Applies gravity as an external force on each dynamic body every
/// simulation step. Attach this to every ndBodyDynamic that should
/// experience gravitational acceleration.
///
class NewtonGravityNotify : public ndBodyNotify
{
public:
    /// Construct with a gravity vector (e.g., {0, -9.81, 0, 0}).
    NewtonGravityNotify(const ndVector &gravity)
        : ndBodyNotify(ndVector::m_zero)  // zero linear/angular damping
        , _gravity(gravity)
    {
    }

    /// Called each physics step — applies gravitational force.
    void OnApplyExternalForce(ndInt32 threadIndex,
                              ndFloat32 timestep) override
    {
        ndBodyDynamic *const body = GetBody()->GetAsBodyDynamic();
        if (body) {
            // F = m * g. Mass is stored in the w component of the
            // mass matrix (diagonal: Ixx, Iyy, Izz, mass).
            const ndVector force(body->GetMassMatrix().m_w * _gravity);
            body->SetForce(force);
        }
    }

    /// Called when the body's transform changes.
    /// Phase 3 will use this to flag dirty transforms for OpenExec
    /// writeback.
    void OnTransform(ndInt32 threadIndex,
                     const ndMatrix &matrix) override
    {
        // TODO(Phase 3): Flag prim as needing transform writeback.
    }

private:
    ndVector _gravity;
};

#endif // NEWTON_DYNAMICS_FOUND

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_BODY_NOTIFY_H

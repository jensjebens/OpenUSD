//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/newtonTypes.h
/// \brief Newton Dynamics 4 ↔ USD type conversion utilities.
///
/// When Newton headers are available (NEWTON_DYNAMICS_FOUND is defined),
/// these functions convert between Newton's ndMatrix/ndVector types and
/// USD's GfMatrix4d/GfVec3d types. When Newton is not available, stub
/// types and identity conversions are provided so the plugin skeleton
/// compiles.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_TYPES_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_TYPES_H

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"

#ifdef NEWTON_DYNAMICS_FOUND
#include <ndNewton.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace NewtonTypes {

#ifdef NEWTON_DYNAMICS_FOUND

/// Convert a Newton ndMatrix to a USD GfMatrix4d.
inline GfMatrix4d
NewtonToUsd(const ndMatrix &m)
{
    // TODO: Phase 2 — implement full matrix conversion.
    // Newton uses column-major, USD uses row-major.
    return GfMatrix4d(1.0);
}

/// Convert a USD GfMatrix4d to a Newton ndMatrix.
inline ndMatrix
UsdToNewton(const GfMatrix4d &m)
{
    // TODO: Phase 2 — implement full matrix conversion.
    return ndGetIdentityMatrix();
}

/// Convert a Newton ndVector to a USD GfVec3d.
inline GfVec3d
NewtonToUsd(const ndVector &v)
{
    // TODO: Phase 2 — implement full vector conversion.
    return GfVec3d(
        static_cast<double>(v.m_x),
        static_cast<double>(v.m_y),
        static_cast<double>(v.m_z));
}

/// Convert a USD GfVec3d to a Newton ndVector.
inline ndVector
UsdToNewton(const GfVec3d &v)
{
    // TODO: Phase 2 — implement full vector conversion.
    return ndVector(
        static_cast<ndFloat32>(v[0]),
        static_cast<ndFloat32>(v[1]),
        static_cast<ndFloat32>(v[2]),
        ndFloat32(0.0f));
}

#else // !NEWTON_DYNAMICS_FOUND

// Stub types when Newton is not available.
// These allow the plugin skeleton to compile without Newton headers.

struct ndMatrix {
    double m[4][4];
};

struct ndVector {
    float m_x, m_y, m_z, m_w;
};

inline GfMatrix4d
NewtonToUsd(const ndMatrix & /*m*/)
{
    return GfMatrix4d(1.0);
}

inline ndMatrix
UsdToNewton(const GfMatrix4d & /*m*/)
{
    ndMatrix identity = {};
    identity.m[0][0] = 1.0;
    identity.m[1][1] = 1.0;
    identity.m[2][2] = 1.0;
    identity.m[3][3] = 1.0;
    return identity;
}

inline GfVec3d
NewtonToUsd(const ndVector &v)
{
    return GfVec3d(
        static_cast<double>(v.m_x),
        static_cast<double>(v.m_y),
        static_cast<double>(v.m_z));
}

inline ndVector
UsdToNewton(const GfVec3d &v)
{
    ndVector result;
    result.m_x = static_cast<float>(v[0]);
    result.m_y = static_cast<float>(v[1]);
    result.m_z = static_cast<float>(v[2]);
    result.m_w = 0.0f;
    return result;
}

#endif // NEWTON_DYNAMICS_FOUND

} // namespace NewtonTypes

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_NEWTON_TYPES_H

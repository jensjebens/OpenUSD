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
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"

#ifdef NEWTON_DYNAMICS_FOUND
#include <ndNewton.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace NewtonTypes {

#ifdef NEWTON_DYNAMICS_FOUND

/// Convert a Newton ndMatrix to a USD GfMatrix4d.
///
/// Newton's ndMatrix stores four column vectors (m_front, m_up, m_right,
/// m_posit) as single-precision floats. USD's GfMatrix4d is row-major
/// double[4][4].
///
/// USD's row-major convention puts translation in the LAST ROW (row 3),
/// while Newton stores it in the m_posit column vector. The correct
/// mapping is: each Newton column vector becomes a ROW in GfMatrix4d.
///   row 0 = m_front (column 0)
///   row 1 = m_up    (column 1)
///   row 2 = m_right (column 2)
///   row 3 = m_posit (column 3)
inline GfMatrix4d
NewtonToUsd(const ndMatrix &m)
{
    return GfMatrix4d(
        m.m_front.m_x, m.m_front.m_y, m.m_front.m_z, m.m_front.m_w,
        m.m_up.m_x,    m.m_up.m_y,    m.m_up.m_z,    m.m_up.m_w,
        m.m_right.m_x, m.m_right.m_y, m.m_right.m_z, m.m_right.m_w,
        m.m_posit.m_x, m.m_posit.m_y, m.m_posit.m_z, m.m_posit.m_w
    );
}

/// Convert a USD GfMatrix4d to a Newton ndMatrix.
///
/// USD row-major layout: rows of GfMatrix4d become Newton column vectors.
///   m_front = row 0 = d[0..3]
///   m_up    = row 1 = d[4..7]
///   m_right = row 2 = d[8..11]
///   m_posit = row 3 = d[12..15]
inline ndMatrix
UsdToNewton(const GfMatrix4d &m)
{
    const double *d = m.GetArray();
    ndMatrix result;
    result.m_front = ndVector(
        ndFloat32(d[0]),  ndFloat32(d[1]),  ndFloat32(d[2]),  ndFloat32(d[3]));
    result.m_up    = ndVector(
        ndFloat32(d[4]),  ndFloat32(d[5]),  ndFloat32(d[6]),  ndFloat32(d[7]));
    result.m_right = ndVector(
        ndFloat32(d[8]),  ndFloat32(d[9]),  ndFloat32(d[10]), ndFloat32(d[11]));
    result.m_posit = ndVector(
        ndFloat32(d[12]), ndFloat32(d[13]), ndFloat32(d[14]), ndFloat32(d[15]));
    return result;
}

/// Convert a Newton ndVector to a USD GfVec3d.
inline GfVec3d
NewtonToUsd(const ndVector &v)
{
    return GfVec3d(
        static_cast<double>(v.m_x),
        static_cast<double>(v.m_y),
        static_cast<double>(v.m_z));
}

/// Convert a USD GfVec3d to a Newton ndVector.
inline ndVector
UsdToNewton(const GfVec3d &v)
{
    return ndVector(
        static_cast<ndFloat32>(v[0]),
        static_cast<ndFloat32>(v[1]),
        static_cast<ndFloat32>(v[2]),
        ndFloat32(0.0f));
}

/// Convert a USD GfQuatd to a Newton ndQuaternion.
///
/// GfQuatd stores (real, imaginary) where imaginary = (i, j, k).
/// ndQuaternion layout is (x, y, z, w) where w is the scalar part.
inline ndQuaternion
UsdToNewton(const GfQuatd &q)
{
    GfVec3d img = q.GetImaginary();
    return ndQuaternion(
        static_cast<ndFloat32>(img[0]),
        static_cast<ndFloat32>(img[1]),
        static_cast<ndFloat32>(img[2]),
        static_cast<ndFloat32>(q.GetReal()));
}

/// Convert a Newton ndQuaternion to a USD GfQuatd.
inline GfQuatd
NewtonToUsd(const ndQuaternion &q)
{
    return GfQuatd(
        static_cast<double>(q.m_w),
        GfVec3d(
            static_cast<double>(q.m_x),
            static_cast<double>(q.m_y),
            static_cast<double>(q.m_z)));
}

/// Convert a USD GfQuatf to a Newton ndQuaternion.
inline ndQuaternion
UsdToNewton(const GfQuatf &q)
{
    GfVec3f img = q.GetImaginary();
    return ndQuaternion(
        static_cast<ndFloat32>(img[0]),
        static_cast<ndFloat32>(img[1]),
        static_cast<ndFloat32>(img[2]),
        static_cast<ndFloat32>(q.GetReal()));
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

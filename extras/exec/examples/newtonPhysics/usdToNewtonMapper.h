//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file newtonPhysics/usdToNewtonMapper.h
/// \brief Maps USD physics prims to Newton Dynamics bodies.
///
/// Traverses a USD stage, identifies prims with PhysicsRigidBodyAPI
/// and/or PhysicsCollisionAPI, and creates corresponding Newton 4 bodies.
/// Supports box, sphere, and capsule shapes; dynamic, kinematic, and
/// static bodies; mass from PhysicsMassAPI; and material properties
/// (friction and restitution) from PhysicsMaterialAPI.

#ifndef EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_USD_TO_NEWTON_MAPPER_H
#define EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_USD_TO_NEWTON_MAPPER_H

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/hashmap.h"
#include "pxr/usd/sdf/path.h"

#ifdef NEWTON_DYNAMICS_FOUND
#include <ndNewton.h>
#endif

#include <cstddef>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \struct PhysicsMaterialProperties
///
/// Stores physics material parameters read from a USD PhysicsMaterialAPI.
/// Default values match the USD physics schema defaults.
///
struct PhysicsMaterialProperties {
    float staticFriction = 0.5f;   ///< Default static friction coefficient.
    float dynamicFriction = 0.5f;  ///< Default dynamic friction coefficient.
    float restitution = 0.0f;      ///< Default restitution (no bounce).
};

/// \class UsdToNewtonMapper
///
/// Traverses a USD stage and maps physics prims to Newton Dynamics
/// bodies. Creates dynamic bodies for prims with PhysicsRigidBodyAPI,
/// kinematic bodies when \c physics:kinematicEnabled is true, and
/// static bodies for prims with only PhysicsCollisionAPI.
///
class UsdToNewtonMapper
{
public:
    UsdToNewtonMapper() = default;

    /// Traverse the stage, find all physics prims, create Newton bodies.
    /// Requires NewtonWorldManager to be initialized first.
    void MapStage(const UsdStageRefPtr &stage);

    /// Clear all mappings and body references.
    void Clear();

    /// Get total body count (dynamic + kinematic + static).
    size_t GetBodyCount() const;

    /// Returns true if the prim at \p path is mapped as a body.
    bool HasBody(const SdfPath &path) const;

    /// Get the number of dynamic (non-kinematic) rigid bodies.
    size_t GetDynamicBodyCount() const;

    /// Get the number of static collision bodies (no RigidBodyAPI).
    size_t GetStaticBodyCount() const;

    /// Query the simulated transform for a prim path.
    /// Returns identity if the prim is not mapped.
    GfMatrix4d GetSimulatedTransform(const SdfPath &path) const;

    /// Returns true if the body at \p path is kinematic.
    bool IsKinematic(const SdfPath &path) const;

    /// Returns paths to all dynamic (non-kinematic, non-static) bodies.
    std::vector<SdfPath> GetDynamicBodyPaths() const;

    /// Returns paths to all mapped bodies (dynamic, kinematic, static).
    std::vector<SdfPath> GetAllBodyPaths() const;

    /// Get material properties for a body at \p path.
    /// Returns default PhysicsMaterialProperties if the body is not found
    /// or no physics material was bound.
    PhysicsMaterialProperties GetMaterialProperties(const SdfPath &path) const;

    /// Update all body records with current simulated transforms from Newton.
    /// Called after NewtonWorldManager::Step() to pull transforms from the
    /// physics engine into the mapper's cached records. In stub mode
    /// (no Newton), this is a no-op — transforms stay at their initial values.
    void UpdateSimulatedTransforms();

private:
    /// Describes a mapped physics body.
    struct BodyRecord {
        SdfPath primPath;
        bool isDynamic = false;
        bool isKinematic = false;
        GfMatrix4d initialTransform;
        GfMatrix4d simulatedTransform;  // updated after Step()
        PhysicsMaterialProperties materialProps;
#ifdef NEWTON_DYNAMICS_FOUND
        ndSharedPtr<ndBody> body;
        ndSharedPtr<ndShapeInstance> shape;  // keep shape alive for body lifetime
#endif
    };

    // Mapping helpers
    void _MapRigidBody(const UsdPrim &prim, const UsdStageRefPtr &stage);
    void _MapStaticCollider(const UsdPrim &prim, const UsdStageRefPtr &stage);

    // Shape creation — returns ndSharedPtr<ndShapeInstance> (Newton 4 pattern)
#ifdef NEWTON_DYNAMICS_FOUND
    ndSharedPtr<ndShapeInstance> _CreateShape(const UsdPrim &prim);
    ndSharedPtr<ndShapeInstance> _CreateBoxShape(const UsdPrim &prim);
    ndSharedPtr<ndShapeInstance> _CreateSphereShape(const UsdPrim &prim);
    ndSharedPtr<ndShapeInstance> _CreateCapsuleShape(const UsdPrim &prim);
#endif

    // Mass / inertia
    float _GetMass(const UsdPrim &prim, float defaultMass = 1.0f);

    // Material properties (friction, restitution)
    PhysicsMaterialProperties _GetMaterialProperties(
        const UsdPrim &prim) const;

    // Transform reading
    GfMatrix4d _GetWorldTransform(const UsdPrim &prim) const;

    TfHashMap<SdfPath, BodyRecord, SdfPath::Hash> _bodyMap;
    size_t _dynamicCount = 0;
    size_t _staticCount = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // EXTRAS_EXEC_EXAMPLES_NEWTON_PHYSICS_USD_TO_NEWTON_MAPPER_H

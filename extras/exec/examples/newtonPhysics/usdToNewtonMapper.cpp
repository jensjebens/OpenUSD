//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "usdToNewtonMapper.h"
#include "newtonWorldManager.h"
#include "newtonTypes.h"

// Newton 4's base ndBodyNotify handles gravity via its constructor —
// no custom notify class needed.

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/cube.h"
#include "pxr/usd/usdGeom/sphere.h"
#include "pxr/usd/usdGeom/capsule.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"
#include "pxr/usd/usdPhysics/collisionAPI.h"
#include "pxr/usd/usdPhysics/massAPI.h"
#include "pxr/usd/usdPhysics/materialAPI.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/usd/usdShade/material.h"

PXR_NAMESPACE_OPEN_SCOPE

// --------------------------------------------------------------------------
// Public interface
// --------------------------------------------------------------------------

void
UsdToNewtonMapper::MapStage(const UsdStageRefPtr &stage)
{
    Clear();

    if (!stage) {
        TF_WARN("UsdToNewtonMapper::MapStage called with null stage.");
        return;
    }

    for (const UsdPrim &prim : stage->Traverse()) {
        if (prim.HasAPI<UsdPhysicsRigidBodyAPI>()) {
            _MapRigidBody(prim, stage);
        }
        else if (prim.HasAPI<UsdPhysicsCollisionAPI>()) {
            _MapStaticCollider(prim, stage);
        }
    }

    TF_STATUS("UsdToNewtonMapper: mapped %zu bodies "
              "(%zu dynamic, %zu static).",
              GetBodyCount(), _dynamicCount, _staticCount);
}

void
UsdToNewtonMapper::Clear()
{
    _bodyMap.clear();
    _dynamicCount = 0;
    _staticCount = 0;
}

size_t
UsdToNewtonMapper::GetBodyCount() const
{
    return _bodyMap.size();
}

bool
UsdToNewtonMapper::HasBody(const SdfPath &path) const
{
    return _bodyMap.count(path) > 0;
}

size_t
UsdToNewtonMapper::GetDynamicBodyCount() const
{
    return _dynamicCount;
}

size_t
UsdToNewtonMapper::GetStaticBodyCount() const
{
    return _staticCount;
}

GfMatrix4d
UsdToNewtonMapper::GetSimulatedTransform(const SdfPath &path) const
{
    auto it = _bodyMap.find(path);
    if (it == _bodyMap.end()) {
        return GfMatrix4d(1.0);
    }

    // Always return the cached simulatedTransform. This is updated by
    // UpdateSimulatedTransforms() after each Newton step, avoiding
    // direct reads from Newton bodies during computation evaluation
    // (which could cause threading issues).
    return it->second.simulatedTransform;
}

void
UsdToNewtonMapper::UpdateSimulatedTransforms()
{
#ifdef NEWTON_DYNAMICS_FOUND
    for (auto &pair : _bodyMap) {
        BodyRecord &rec = pair.second;
        if (rec.body) {
            const ndBodyKinematic *kinBody =
                rec.body->GetAsBodyKinematic();
            if (kinBody) {
                rec.simulatedTransform =
                    NewtonTypes::NewtonToUsd(kinBody->GetMatrix());
            }
        }
    }
#endif
    // In stub mode (no Newton), transforms stay at their initial values.
}

bool
UsdToNewtonMapper::IsKinematic(const SdfPath &path) const
{
    auto it = _bodyMap.find(path);
    if (it == _bodyMap.end()) {
        return false;
    }
    return it->second.isKinematic;
}

std::vector<SdfPath>
UsdToNewtonMapper::GetDynamicBodyPaths() const
{
    std::vector<SdfPath> paths;
    for (const auto &entry : _bodyMap) {
        if (entry.second.isDynamic && !entry.second.isKinematic) {
            paths.push_back(entry.first);
        }
    }
    return paths;
}

std::vector<SdfPath>
UsdToNewtonMapper::GetAllBodyPaths() const
{
    std::vector<SdfPath> paths;
    paths.reserve(_bodyMap.size());
    for (const auto &entry : _bodyMap) {
        paths.push_back(entry.first);
    }
    return paths;
}

PhysicsMaterialProperties
UsdToNewtonMapper::GetMaterialProperties(const SdfPath &path) const
{
    auto it = _bodyMap.find(path);
    if (it == _bodyMap.end()) {
        return PhysicsMaterialProperties();
    }
    return it->second.materialProps;
}

// --------------------------------------------------------------------------
// Mapping helpers
// --------------------------------------------------------------------------

void
UsdToNewtonMapper::_MapRigidBody(const UsdPrim &prim,
                                  const UsdStageRefPtr &stage)
{
    // Read kinematic flag.
    UsdPhysicsRigidBodyAPI rigidBodyAPI(prim);
    bool kinematicEnabled = false;
    rigidBodyAPI.GetKinematicEnabledAttr().Get(&kinematicEnabled);

    // World transform.
    GfMatrix4d worldXform = _GetWorldTransform(prim);

    // Mass (only meaningful for dynamic bodies).
    float mass = _GetMass(prim);

    // Material properties (friction, restitution).
    PhysicsMaterialProperties matProps = _GetMaterialProperties(prim);

    BodyRecord rec;
    rec.primPath = prim.GetPath();
    rec.isDynamic = !kinematicEnabled;
    rec.isKinematic = kinematicEnabled;
    rec.initialTransform = worldXform;
    rec.simulatedTransform = worldXform;
    rec.materialProps = matProps;

#ifdef NEWTON_DYNAMICS_FOUND
    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();

    ndSharedPtr<ndShapeInstance> shape = _CreateShape(prim);

    if (kinematicEnabled) {
        // Kinematic body — driven by animation, not forces.
        ndSharedPtr<ndBody> body(new ndBodyKinematic());
        body->GetAsBodyKinematic()->SetCollisionShape(**shape);
        body->SetMatrix(NewtonTypes::UsdToNewton(worldXform));

        rec.body = body;
        rec.shape = shape;
        mgr.AddBody(body);
    }
    else {
        // Dynamic body — participates in simulation.
        ndSharedPtr<ndBody> body(new ndBodyDynamic());

        // Use base ndBodyNotify for gravity — this is the correct Newton 4
        // pattern. The base class handles gravity in OnApplyExternalForce.
        GfVec3d grav = mgr.GetGravity();
        body->SetNotifyCallback(new ndBodyNotify(
            ndVector(static_cast<ndFloat32>(grav[0]),
                     static_cast<ndFloat32>(grav[1]),
                     static_cast<ndFloat32>(grav[2]),
                     0.0f)));

        body->SetMatrix(NewtonTypes::UsdToNewton(worldXform));
        body->GetAsBodyKinematic()->SetCollisionShape(**shape);
        body->GetAsBodyKinematic()->SetMassMatrix(mass, **shape);

        rec.body = body;
        rec.shape = shape;
        mgr.AddBody(body);
    }

    // Newton 4 handles material properties through contact callbacks
    // (ndContactNotify) rather than per-shape settings. Material
    // properties are stored in the BodyRecord for now.
#endif

    _bodyMap[prim.GetPath()] = std::move(rec);

    // Both kinematic and dynamic bodies with RigidBodyAPI count as
    // "dynamic" for accounting purposes (vs. purely static colliders).
    _dynamicCount++;

    TF_STATUS("Mapped rigid body: %s (%s, mass=%.2f, "
              "friction=%.2f/%.2f, restitution=%.2f)",
              prim.GetPath().GetText(),
              kinematicEnabled ? "kinematic" : "dynamic",
              mass,
              matProps.staticFriction,
              matProps.dynamicFriction,
              matProps.restitution);
}

void
UsdToNewtonMapper::_MapStaticCollider(const UsdPrim &prim,
                                       const UsdStageRefPtr &stage)
{
    GfMatrix4d worldXform = _GetWorldTransform(prim);

    // Material properties (friction, restitution).
    PhysicsMaterialProperties matProps = _GetMaterialProperties(prim);

    BodyRecord rec;
    rec.primPath = prim.GetPath();
    rec.isDynamic = false;
    rec.isKinematic = false;
    rec.initialTransform = worldXform;
    rec.simulatedTransform = worldXform;
    rec.materialProps = matProps;

#ifdef NEWTON_DYNAMICS_FOUND
    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();

    ndSharedPtr<ndShapeInstance> shape = _CreateShape(prim);

    // Static body — a kinematic body with zero velocity.
    ndSharedPtr<ndBody> body(new ndBodyKinematic());
    body->GetAsBodyKinematic()->SetCollisionShape(**shape);
    body->SetMatrix(NewtonTypes::UsdToNewton(worldXform));

    rec.body = body;
    rec.shape = shape;
    mgr.AddBody(body);
#endif

    _bodyMap[prim.GetPath()] = std::move(rec);
    _staticCount++;

    TF_STATUS("Mapped static collider: %s (friction=%.2f/%.2f, "
              "restitution=%.2f)",
              prim.GetPath().GetText(),
              matProps.staticFriction,
              matProps.dynamicFriction,
              matProps.restitution);
}

// --------------------------------------------------------------------------
// Shape creation
// --------------------------------------------------------------------------

#ifdef NEWTON_DYNAMICS_FOUND

ndSharedPtr<ndShapeInstance>
UsdToNewtonMapper::_CreateShape(const UsdPrim &prim)
{
    if (prim.IsA<UsdGeomCube>()) {
        return _CreateBoxShape(prim);
    }
    else if (prim.IsA<UsdGeomSphere>()) {
        return _CreateSphereShape(prim);
    }
    else if (prim.IsA<UsdGeomCapsule>()) {
        return _CreateCapsuleShape(prim);
    }

    TF_WARN("Unsupported collision shape for prim '%s' (type '%s'). "
             "Using unit box as fallback.",
             prim.GetPath().GetText(),
             prim.GetTypeName().GetText());

    return ndSharedPtr<ndShapeInstance>(
        new ndShapeInstance(new ndShapeBox(1.0f, 1.0f, 1.0f)));
}

ndSharedPtr<ndShapeInstance>
UsdToNewtonMapper::_CreateBoxShape(const UsdPrim &prim)
{
    double size = 1.0;
    UsdGeomCube cube(prim);
    cube.GetSizeAttr().Get(&size);

    // Read scale from xform ops, if present.
    GfVec3f scale(1.0f, 1.0f, 1.0f);
    UsdGeomXformable xformable(prim);
    bool resetsXformStack = false;
    std::vector<UsdGeomXformOp> xformOps = xformable.GetOrderedXformOps(
        &resetsXformStack);
    for (const UsdGeomXformOp &op : xformOps) {
        if (op.GetOpType() == UsdGeomXformOp::TypeScale) {
            op.Get(&scale);
            break;
        }
    }

    // ndShapeBox takes full extents.
    float ex = static_cast<float>(size) * scale[0];
    float ey = static_cast<float>(size) * scale[1];
    float ez = static_cast<float>(size) * scale[2];

    return ndSharedPtr<ndShapeInstance>(
        new ndShapeInstance(new ndShapeBox(ex, ey, ez)));
}

ndSharedPtr<ndShapeInstance>
UsdToNewtonMapper::_CreateSphereShape(const UsdPrim &prim)
{
    double radius = 1.0;
    UsdGeomSphere sphere(prim);
    sphere.GetRadiusAttr().Get(&radius);

    return ndSharedPtr<ndShapeInstance>(
        new ndShapeInstance(
            new ndShapeSphere(static_cast<float>(radius))));
}

ndSharedPtr<ndShapeInstance>
UsdToNewtonMapper::_CreateCapsuleShape(const UsdPrim &prim)
{
    double height = 1.0;
    double radius = 0.5;
    UsdGeomCapsule capsule(prim);
    capsule.GetHeightAttr().Get(&height);
    capsule.GetRadiusAttr().Get(&radius);

    // Newton capsule: (radius0, radius1, height between centers).
    return ndSharedPtr<ndShapeInstance>(
        new ndShapeInstance(new ndShapeCapsule(
            static_cast<float>(radius),
            static_cast<float>(radius),
            static_cast<float>(height))));
}

#endif // NEWTON_DYNAMICS_FOUND

// --------------------------------------------------------------------------
// Mass
// --------------------------------------------------------------------------

float
UsdToNewtonMapper::_GetMass(const UsdPrim &prim, float defaultMass)
{
    if (prim.HasAPI<UsdPhysicsMassAPI>()) {
        UsdPhysicsMassAPI massAPI(prim);

        float mass = 0.0f;
        massAPI.GetMassAttr().Get(&mass);
        if (mass > 0.0f) {
            return mass;
        }

        // Fall back to density (approximate — volume not computed for POC).
        float density = 0.0f;
        massAPI.GetDensityAttr().Get(&density);
        if (density > 0.0f) {
            // Approximation: uses density as mass directly.
            // For accuracy, multiply by actual shape volume.
            return density;
        }
    }
    return defaultMass;
}

// --------------------------------------------------------------------------
// Material properties
// --------------------------------------------------------------------------

PhysicsMaterialProperties
UsdToNewtonMapper::_GetMaterialProperties(const UsdPrim &prim) const
{
    PhysicsMaterialProperties props;

    UsdShadeMaterialBindingAPI bindingAPI(prim);
    if (!bindingAPI) {
        return props;
    }

    // Resolve the physics-purpose material binding.
    TfToken physicsPurpose("physics");
    UsdShadeMaterial material =
        bindingAPI.ComputeBoundMaterial(physicsPurpose);
    if (!material) {
        return props;
    }

    UsdPrim matPrim = material.GetPrim();
    if (!matPrim.HasAPI<UsdPhysicsMaterialAPI>()) {
        return props;
    }

    UsdPhysicsMaterialAPI materialAPI(matPrim);
    materialAPI.GetStaticFrictionAttr().Get(&props.staticFriction);
    materialAPI.GetDynamicFrictionAttr().Get(&props.dynamicFriction);
    materialAPI.GetRestitutionAttr().Get(&props.restitution);

    return props;
}

// --------------------------------------------------------------------------
// Transform
// --------------------------------------------------------------------------

GfMatrix4d
UsdToNewtonMapper::_GetWorldTransform(const UsdPrim &prim) const
{
    UsdGeomXformable xformable(prim);
    if (!xformable) {
        return GfMatrix4d(1.0);
    }

    // Compute full local-to-world (handles parent transforms).
    return xformable.ComputeLocalToWorldTransform(
        UsdTimeCode::Default());
}

PXR_NAMESPACE_CLOSE_SCOPE

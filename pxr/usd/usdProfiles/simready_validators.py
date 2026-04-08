#!/usr/bin/env python3
"""
SimReady Mini Profile — UsdValidation Plugin (Python)
=====================================================

Registers 10 SimReady validation rules as UsdValidation validators,
covering a mini version of the Prop-Robotics-Neutral profile.

Capabilities covered:
  - com.nvidia.simready.geom (4 rules)
  - com.nvidia.simready.physics.rigidBodies (3 rules)
  - com.nvidia.simready.hierarchy (2 rules)
  - com.nvidia.simready.units (1 rule)

Usage:
    export PYTHONPATH=/path/to/usd_profiles_build/lib/python
    python3 simready_validators.py my_asset.usda

Or import and use programmatically:

    from simready_validators import register_simready_validators, validate_asset
    register_simready_validators()
    errors = validate_asset("my_asset.usda")
"""

from __future__ import print_function

import sys
from pxr import Gf, Sdf, Tf, Usd, UsdGeom, UsdPhysics, UsdValidation

# ---------------------------------------------------------------------------
# Validator implementations
# ---------------------------------------------------------------------------

# --- Geometry validators (com.nvidia.simready.geom) ---

def _check_geom_shall_be_mesh(prim, timeRange):
    """VG.MESH.001: All geometry shall be UsdGeomMesh (non-subdivided)."""
    errors = []
    if prim.IsA(UsdGeom.Gprim) and not prim.IsA(UsdGeom.Mesh):
        errors.append(UsdValidation.ValidationError(
            "NonMeshGeometry",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Prim '{prim.GetPath()}' is a {prim.GetTypeName()}, not a Mesh. "
            f"SimReady requires all geometry as UsdGeomMesh.",
        ))
    return errors


def _check_mesh_topology(prim, timeRange):
    """VG.014: Mesh topology must be valid (faceVertexCounts and faceVertexIndices consistent)."""
    errors = []
    if not prim.IsA(UsdGeom.Mesh):
        return errors

    mesh = UsdGeom.Mesh(prim)
    fvc = mesh.GetFaceVertexCountsAttr().Get()
    fvi = mesh.GetFaceVertexIndicesAttr().Get()
    points = mesh.GetPointsAttr().Get()

    if fvc is None or fvi is None:
        errors.append(UsdValidation.ValidationError(
            "MissingTopology",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Mesh '{prim.GetPath()}' is missing faceVertexCounts or faceVertexIndices.",
        ))
        return errors

    # Check that sum of faceVertexCounts equals length of faceVertexIndices
    expected_indices = sum(fvc)
    if expected_indices != len(fvi):
        errors.append(UsdValidation.ValidationError(
            "TopologyMismatch",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Mesh '{prim.GetPath()}': sum of faceVertexCounts ({expected_indices}) "
            f"!= length of faceVertexIndices ({len(fvi)}).",
        ))

    # Check that indices don't reference out-of-range points
    if points is not None and fvi:
        max_idx = max(fvi)
        if max_idx >= len(points):
            errors.append(UsdValidation.ValidationError(
                "IndexOutOfRange",
                UsdValidation.ValidationErrorType.Error,
                [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
                f"Mesh '{prim.GetPath()}': faceVertexIndex {max_idx} exceeds "
                f"points count ({len(points)}).",
            ))

    return errors


def _check_mesh_normals_exist(prim, timeRange):
    """VG.027: All non-subdivided meshes must have normals."""
    errors = []
    if not prim.IsA(UsdGeom.Mesh):
        return errors

    mesh = UsdGeom.Mesh(prim)
    subdiv = mesh.GetSubdivisionSchemeAttr().Get()

    # If subdivision is "none" (or not catmullClark/loop), normals are required
    if subdiv in (None, "none", UsdGeom.Tokens.none):
        normals = mesh.GetNormalsAttr().Get()
        if normals is None or len(normals) == 0:
            errors.append(UsdValidation.ValidationError(
                "MissingNormals",
                UsdValidation.ValidationErrorType.Error,
                [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
                f"Mesh '{prim.GetPath()}' has no normals. "
                f"Non-subdivided meshes must have explicit normals.",
            ))

    return errors


def _check_asset_at_origin(prim, timeRange):
    """VG.025: The asset root must be at the origin."""
    errors = []
    # Only check the default prim (asset root)
    stage = prim.GetStage()
    default_prim = stage.GetDefaultPrim()
    if not default_prim or prim != default_prim:
        return errors

    xformable = UsdGeom.Xformable(prim)
    if not xformable:
        return errors

    xform = xformable.GetLocalTransformation()
    identity = Gf.Matrix4d(1.0)
    if not Gf.IsClose(xform, identity, 1e-6):
        errors.append(UsdValidation.ValidationError(
            "AssetNotAtOrigin",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Asset root '{prim.GetPath()}' is not at the origin. "
            f"Transform should be identity.",
        ))

    return errors


# --- Physics validators (com.nvidia.simready.physics.rigidBodies) ---

def _check_rigid_body_is_xformable(prim, timeRange):
    """RB.003: Rigid bodies must be UsdGeomXformable prims."""
    errors = []
    if not prim.HasAPI(UsdPhysics.RigidBodyAPI):
        return errors

    if not prim.IsA(UsdGeom.Xformable):
        errors.append(UsdValidation.ValidationError(
            "RigidBodyNotXformable",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Rigid body '{prim.GetPath()}' is not a UsdGeomXformable prim.",
        ))

    return errors


def _check_rigid_body_no_instancing(prim, timeRange):
    """RB.005: Rigid bodies cannot be part of a scene graph instance."""
    errors = []
    if not prim.HasAPI(UsdPhysics.RigidBodyAPI):
        return errors

    if prim.IsInstanceProxy() or prim.IsInPrototype():
        errors.append(UsdValidation.ValidationError(
            "RigidBodyInstanced",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Rigid body '{prim.GetPath()}' is part of a scene graph instance. "
            f"Rigid bodies cannot be instanced.",
        ))

    return errors


def _check_collider_has_api(prim, timeRange):
    """RB.COL.001: Colliding Gprims must have the CollisionAPI applied."""
    errors = []
    # This is an "essential" check — we look for physics-related prims
    # that seem like they should have CollisionAPI but don't.
    # Specifically: if a prim is under a RigidBodyAPI and is a Gprim,
    # it should have CollisionAPI.
    if not prim.IsA(UsdGeom.Gprim):
        return errors

    # Walk up to find if we're under a rigid body
    parent = prim.GetParent()
    under_rigid_body = False
    while parent and parent.GetPath() != Sdf.Path.absoluteRootPath:
        if parent.HasAPI(UsdPhysics.RigidBodyAPI):
            under_rigid_body = True
            break
        parent = parent.GetParent()

    if under_rigid_body and not prim.HasAPI(UsdPhysics.CollisionAPI):
        errors.append(UsdValidation.ValidationError(
            "MissingCollisionAPI",
            UsdValidation.ValidationErrorType.Warn,
            [UsdValidation.ValidationErrorSite(prim.GetStage(), prim.GetPath())],
            f"Gprim '{prim.GetPath()}' is under a rigid body but does not have "
            f"CollisionAPI applied. Consider adding UsdPhysicsCollisionAPI.",
        ))

    return errors


# --- Hierarchy validators (com.nvidia.simready.hierarchy) ---

def _check_hierarchy_has_root(stage, timeRange):
    """HI.001: Prim hierarchy must have a single root prim."""
    errors = []
    root_prims = [p for p in stage.GetPseudoRoot().GetChildren() if p.IsActive()]

    if len(root_prims) == 0:
        errors.append(UsdValidation.ValidationError(
            "NoRootPrim",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(stage, Sdf.Path.absoluteRootPath)],
            "Stage has no active root prims.",
        ))
    elif len(root_prims) > 1:
        names = [str(p.GetPath()) for p in root_prims]
        errors.append(UsdValidation.ValidationError(
            "MultipleRootPrims",
            UsdValidation.ValidationErrorType.Warn,
            [UsdValidation.ValidationErrorSite(stage, Sdf.Path.absoluteRootPath)],
            f"Stage has {len(root_prims)} root prims ({', '.join(names)}). "
            f"SimReady assets should have a single root prim.",
        ))

    return errors


def _check_default_prim(stage, timeRange):
    """HI.004: Stage must specify a default prim."""
    errors = []
    default_prim = stage.GetDefaultPrim()
    if not default_prim or not default_prim.IsValid():
        errors.append(UsdValidation.ValidationError(
            "MissingDefaultPrim",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(stage, Sdf.Path.absoluteRootPath)],
            "Stage does not specify a default prim. "
            "SimReady assets must have a defaultPrim for reliable referencing.",
        ))

    return errors


# --- Units validators (com.nvidia.simready.units) ---

def _check_upaxis(stage, timeRange):
    """UN.001: Stage must specify upAxis."""
    errors = []
    if not stage.HasAuthoredMetadata(UsdGeom.Tokens.upAxis):
        errors.append(UsdValidation.ValidationError(
            "MissingUpAxis",
            UsdValidation.ValidationErrorType.Error,
            [UsdValidation.ValidationErrorSite(stage, Sdf.Path.absoluteRootPath)],
            "Stage does not specify upAxis metadata. "
            "SimReady requires upAxis to be declared.",
        ))

    return errors


# ---------------------------------------------------------------------------
# Registration
# ---------------------------------------------------------------------------

# Validator registry: maps requirement code to (metadata, function, granularity)
SIMREADY_VALIDATORS = [
    # (name, doc, keywords, schemaTypes, function, granularity)
    # granularity: "prim" or "stage"

    # Geometry
    ("com.nvidia.simready:VG.MESH.001",
     "All geometry shall be represented as UsdGeomMesh",
     ["simready", "geom", "essential"],
     ["UsdGeomGprim"],
     _check_geom_shall_be_mesh, "prim"),

    ("com.nvidia.simready:VG.014",
     "Mesh topology must be valid (faceVertexCounts/Indices consistent)",
     ["simready", "geom", "correctness"],
     ["UsdGeomMesh"],
     _check_mesh_topology, "prim"),

    ("com.nvidia.simready:VG.027",
     "All non-subdivided meshes must have normals",
     ["simready", "geom", "correctness"],
     ["UsdGeomMesh"],
     _check_mesh_normals_exist, "prim"),

    ("com.nvidia.simready:VG.025",
     "Asset root must be positioned at the origin",
     ["simready", "geom", "essential"],
     ["UsdGeomXformable"],
     _check_asset_at_origin, "prim"),

    # Physics
    ("com.nvidia.simready:RB.003",
     "Rigid bodies must be UsdGeomXformable prims",
     ["simready", "physics", "correctness"],
     ["UsdPhysicsRigidBodyAPI"],
     _check_rigid_body_is_xformable, "prim"),

    ("com.nvidia.simready:RB.005",
     "Rigid bodies cannot be part of a scene graph instance",
     ["simready", "physics", "correctness"],
     ["UsdPhysicsRigidBodyAPI"],
     _check_rigid_body_no_instancing, "prim"),

    ("com.nvidia.simready:RB.COL.001",
     "Colliding Gprims under rigid bodies should have CollisionAPI applied",
     ["simready", "physics", "essential"],
     ["UsdGeomGprim"],
     _check_collider_has_api, "prim"),

    # Hierarchy
    ("com.nvidia.simready:HI.001",
     "Prim hierarchy must have a single root prim",
     ["simready", "hierarchy", "essential"],
     [],
     _check_hierarchy_has_root, "stage"),

    ("com.nvidia.simready:HI.004",
     "Stage must specify a default prim",
     ["simready", "hierarchy", "essential"],
     [],
     _check_default_prim, "stage"),

    # Units
    ("com.nvidia.simready:UN.001",
     "Stage must specify upAxis metadata",
     ["simready", "units", "essential"],
     [],
     _check_upaxis, "stage"),
]


_registered = False

def register_simready_validators():
    """Register all SimReady validators with the UsdValidation registry."""
    global _registered
    if _registered:
        return UsdValidation.ValidationRegistry()
    _registered = True

    registry = UsdValidation.ValidationRegistry()

    for name, doc, keywords, schemaTypes, fn, granularity in SIMREADY_VALIDATORS:
        metadata = UsdValidation.ValidatorMetadata(
            name=name,
            doc=doc,
            keywords=keywords,
            schemaTypes=schemaTypes,
        )

        if granularity == "prim":
            registry.RegisterPrimValidator(metadata, fn)
        elif granularity == "stage":
            registry.RegisterStageValidator(metadata, fn)

    # Register a suite for the mini prop profile
    all_validators = [
        registry.GetOrLoadValidatorByName(name)
        for name, *_ in SIMREADY_VALIDATORS
    ]
    suite_metadata = UsdValidation.ValidatorMetadata(
        name="com.nvidia.simready:PropRoboticsNeutralMini",
        doc="Mini SimReady Prop-Robotics-Neutral profile (10 rules)",
        keywords=["simready", "profile"],
        isSuite=True,
    )
    registry.RegisterValidatorSuite(suite_metadata, all_validators)

    return registry


def validate_asset(asset_path, verbose=True):
    """Validate an asset against the mini SimReady prop profile."""
    registry = register_simready_validators()

    # Get all simready validators
    validators = registry.GetOrLoadValidatorsByName([
        name for name, *_ in SIMREADY_VALIDATORS
    ])

    # Open stage
    stage = Usd.Stage.Open(asset_path)
    if not stage:
        print(f"ERROR: Cannot open {asset_path}")
        return []

    # Create validation context and run
    ctx = UsdValidation.ValidationContext(validators)
    errors = ctx.Validate(stage)

    if verbose:
        if errors:
            print(f"\n{'='*60}")
            print(f"SimReady Validation: {asset_path}")
            print(f"{'='*60}")
            for i, error in enumerate(errors, 1):
                sites = error.GetSites()
                site_str = str(sites[0].GetPrim().GetPath()) if sites and sites[0].GetPrim() else "stage"
                print(f"\n  [{i}] {error.GetIdentifier()}")
                print(f"      Site: {site_str}")
                print(f"      Message: {error.GetMessage()}")
            print(f"\n{'='*60}")
            print(f"Total: {len(errors)} issue(s)")
        else:
            print(f"✅ {asset_path}: All SimReady checks passed!")

    return errors


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <asset.usda> [--create-test-scene]")
        sys.exit(1)

    if sys.argv[1] == "--create-test-scene":
        # Create a test scene with some deliberate issues
        stage = Usd.Stage.CreateNew("test_simready.usda")
        stage.SetDefaultPrim(stage.DefinePrim("/Robot"))
        # No upAxis — should trigger UN.001
        # No metersPerUnit — not checked in mini profile

        root = UsdGeom.Xform.Define(stage, "/Robot")

        # Good mesh (has normals, valid topology)
        mesh = UsdGeom.Mesh.Define(stage, "/Robot/Body")
        mesh.GetPointsAttr().Set([(0,0,0), (1,0,0), (1,1,0), (0,1,0)])
        mesh.GetFaceVertexCountsAttr().Set([4])
        mesh.GetFaceVertexIndicesAttr().Set([0, 1, 2, 3])
        mesh.GetNormalsAttr().Set([(0,0,1), (0,0,1), (0,0,1), (0,0,1)])

        # Bad mesh — no normals (should trigger VG.027)
        bad_mesh = UsdGeom.Mesh.Define(stage, "/Robot/Arm")
        bad_mesh.GetPointsAttr().Set([(0,0,0), (1,0,0), (0,1,0)])
        bad_mesh.GetFaceVertexCountsAttr().Set([3])
        bad_mesh.GetFaceVertexIndicesAttr().Set([0, 1, 2])
        # No normals!

        # Non-mesh geometry (should trigger VG.MESH.001)
        UsdGeom.Sphere.Define(stage, "/Robot/Sensor")

        # Rigid body on the root
        UsdPhysics.RigidBodyAPI.Apply(root.GetPrim())

        # Gprim under rigid body without CollisionAPI (should trigger RB.COL.001)
        # (the meshes above are under /Robot which has RigidBodyAPI)

        stage.Save()
        print("Created test_simready.usda with deliberate issues")
        print("Now run: python3 simready_validators.py test_simready.usda")
    else:
        validate_asset(sys.argv[1])

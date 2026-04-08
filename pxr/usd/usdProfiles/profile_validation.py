#!/usr/bin/env python3
"""
Profile-Driven Validation
=========================

Connects ProfileAPI (capability declarations on prims) to UsdValidation
(validator execution). A profile on a prim determines which validators run.

Usage:
    from profile_validation import validate_against_profile

    # Validate using the profile authored on the stage's default prim
    errors = validate_against_profile("my_asset.usda")

    # Or specify a profile explicitly
    errors = validate_with_profile("my_asset.usda",
                                    "com.nvidia.simready.prop_robotics_neutral")

CLI:
    python3 profile_validation.py my_asset.usda
    python3 profile_validation.py my_asset.usda --profile com.nvidia.simready.prop_robotics_neutral
"""

from __future__ import print_function

import sys
from pxr import Sdf, Usd, UsdProfiles, UsdValidation

# Import to trigger validator registration
import simready_validators

# ---------------------------------------------------------------------------
# Capability → Validator resolution (via CapabilityRegistry)
# ---------------------------------------------------------------------------

def _get_validators_for_profile(profile_name, registry=None):
    """
    Given a profile name, resolve its capabilities via the DAG and return
    the matching UsdValidation validators.

    The capability→validator mapping is declared in plugInfo.json alongside
    the capability definitions (no hardcoded keyword mapping needed).
    """
    if registry is None:
        registry = UsdValidation.ValidationRegistry()

    cap_registry = UsdProfiles.CapabilityRegistry.GetInstance()

    if not cap_registry.IsCapability(profile_name):
        print(f"WARNING: '{profile_name}' is not a registered capability or profile")
        return [], [], []

    # Get all validator names for this capability/profile (including
    # transitive predecessors) — this is the single source of truth.
    validator_names = [
        str(v) for v in cap_registry.GetAllValidatorsForCapability(profile_name)
    ]

    # Resolve capabilities for display
    capabilities = [profile_name] + [
        str(c) for c in cap_registry.GetTransitivePredecessors(profile_name)
    ]

    # Load the actual UsdValidation validator objects
    validators = registry.GetOrLoadValidatorsByName(validator_names)

    return validators, capabilities, validator_names


def validate_against_profile(asset_path, verbose=True):
    """
    Validate an asset using the profile authored on its default prim.

    Reads ProfileAPI from the stage's default prim, resolves capabilities,
    selects matching validators, and runs them.
    """
    # Register validators
    simready_validators.register_simready_validators()
    registry = UsdValidation.ValidationRegistry()

    # Open stage
    stage = Usd.Stage.Open(asset_path)
    if not stage:
        print(f"ERROR: Cannot open {asset_path}")
        return []

    # Find the profile from the stage
    default_prim = stage.GetDefaultPrim()
    if default_prim and default_prim.IsValid():
        profile = UsdProfiles.GetEffectiveProfile(default_prim)
    else:
        # Try the first root prim
        for prim in stage.GetPseudoRoot().GetChildren():
            profile = UsdProfiles.GetEffectiveProfile(prim)
            if profile:
                break
        else:
            profile = None

    if not profile:
        if verbose:
            print(f"No profile found on {asset_path}. "
                  f"Apply ProfileAPI to the default prim to enable profile-driven validation.")
        return []

    return _validate_with_resolved_profile(stage, str(profile), asset_path, registry, verbose)


def validate_with_profile(asset_path, profile_name, verbose=True):
    """
    Validate an asset against a specific profile (regardless of what's authored).
    """
    simready_validators.register_simready_validators()
    registry = UsdValidation.ValidationRegistry()

    stage = Usd.Stage.Open(asset_path)
    if not stage:
        print(f"ERROR: Cannot open {asset_path}")
        return []

    return _validate_with_resolved_profile(stage, profile_name, asset_path, registry, verbose)


def _validate_with_resolved_profile(stage, profile_name, asset_path, registry, verbose):
    """Run validation with a resolved profile."""
    validators, capabilities, keywords = _get_validators_for_profile(profile_name, registry)

    if verbose:
        print(f"\n{'='*70}")
        print(f"Profile-Driven Validation: {asset_path}")
        print(f"{'='*70}")
        print(f"  Profile:      {profile_name}")
        print(f"  Capabilities: {capabilities}")
        print(f"  Validators:   {len(validators)} (from capability DAG)")
        for v in validators:
            md = v.GetMetadata()
            print(f"    - {md.name}: {md.doc}")

    # Run validation
    if not validators:
        if verbose:
            print(f"\n  No validators found for profile '{profile_name}'")
        return []

    ctx = UsdValidation.ValidationContext(validators)
    errors = ctx.Validate(stage)

    if verbose:
        print(f"\n{'─'*70}")
        if errors:
            for i, error in enumerate(errors, 1):
                sites = error.GetSites()
                if sites and sites[0].GetPrim() and sites[0].GetPrim().IsValid():
                    site_str = str(sites[0].GetPrim().GetPath())
                else:
                    site_str = "stage"
                print(f"  [{i}] {error.GetIdentifier()}")
                print(f"      Site:    {site_str}")
                print(f"      Message: {error.GetMessage()}")
                print()
            print(f"{'='*70}")
            print(f"RESULT: {len(errors)} issue(s) found")
        else:
            print(f"{'='*70}")
            print(f"RESULT: ✅ All checks passed for profile '{profile_name}'")

    return errors


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Validate a USD asset against a SimReady profile")
    parser.add_argument("asset", help="Path to USD asset")
    parser.add_argument("--profile", "-p",
                       help="Profile to validate against (default: read from asset's ProfileAPI)")
    parser.add_argument("--create-demo", action="store_true",
                       help="Create a demo asset with ProfileAPI authored")

    args = parser.parse_args()

    if args.create_demo:
        from pxr import UsdGeom, UsdPhysics

        stage = Usd.Stage.CreateNew(args.asset)
        stage.SetMetadata("upAxis", "Z")
        stage.SetMetadata("metersPerUnit", 1.0)

        root = UsdGeom.Xform.Define(stage, "/Robot")
        stage.SetDefaultPrim(root.GetPrim())

        # Apply SimReady profile
        api = UsdProfiles.ProfileAPI.Apply(root.GetPrim())
        api.GetProfileAttr().Set("com.nvidia.simready.prop_robotics_neutral")

        # Add physics
        UsdPhysics.RigidBodyAPI.Apply(root.GetPrim())

        # Valid mesh with normals + CollisionAPI
        mesh = UsdGeom.Mesh.Define(stage, "/Robot/Body")
        mesh.GetPointsAttr().Set([(0,0,0), (1,0,0), (1,1,0), (0,1,0)])
        mesh.GetFaceVertexCountsAttr().Set([4])
        mesh.GetFaceVertexIndicesAttr().Set([0, 1, 2, 3])
        mesh.GetNormalsAttr().Set([(0,0,1)] * 4)
        mesh.GetSubdivisionSchemeAttr().Set("none")
        UsdPhysics.CollisionAPI.Apply(mesh.GetPrim())

        # Invalid: Sphere (not a mesh) without CollisionAPI
        UsdGeom.Sphere.Define(stage, "/Robot/Sensor")

        stage.Save()
        print(f"Created {args.asset} with ProfileAPI")
        print(f"\nUSDA content:")
        print(stage.GetRootLayer().ExportToString())
        print(f"\nNow validate: python3 profile_validation.py {args.asset}")
    elif args.profile:
        validate_with_profile(args.asset, args.profile)
    else:
        validate_against_profile(args.asset)

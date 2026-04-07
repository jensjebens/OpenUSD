#!/usr/bin/env python3
"""
USD Profiles — Demo Script
===========================

Demonstrates the ProfileAPI, CapabilityRegistry, and query system
using a scenario inspired by the USD-Profiles proposal: a robotics
warehouse with assets at different profile levels.

Setup:
    export PYTHONPATH=/path/to/usd_profiles_build/lib/python
    export LD_LIBRARY_PATH=/path/to/usd_profiles_build/lib

    python3 demo_profiles.py

See: https://github.com/PixarAnimationStudios/OpenUSD-proposals/tree/main/proposals/profiles
"""

from pxr import Gf, Sdf, Usd, UsdGeom, UsdProfiles


def main():
    print("=" * 70)
    print("USD Profiles — Demo")
    print("=" * 70)

    # ------------------------------------------------------------------
    # 1. Explore the capability DAG
    # ------------------------------------------------------------------
    print("\n--- 1. Capability Registry ---\n")

    registry = UsdProfiles.CapabilityRegistry.GetInstance()

    print(f"Registered capabilities: {len(registry.GetAllCapabilities())}")
    for cap in sorted(str(c) for c in registry.GetAllCapabilities()):
        is_profile = registry.IsProfile(cap)
        preds = [str(p) for p in registry.GetPredecessors(cap)]
        tag = " [PROFILE]" if is_profile else ""
        print(f"  {cap}{tag}")
        if preds:
            print(f"    predecessors: {preds}")

    print(f"\nRegistered profiles:")
    for profile in registry.GetAllProfiles():
        trans = [str(p) for p in registry.GetTransitivePredecessors(str(profile))]
        print(f"  {profile}")
        print(f"    full capability set: {trans}")

    # ------------------------------------------------------------------
    # 2. Author a warehouse scene with profiles
    # ------------------------------------------------------------------
    print("\n--- 2. Author a warehouse scene ---\n")

    stage = Usd.Stage.CreateNew("warehouse.usda")
    stage.SetMetadata("upAxis", "Z")
    stage.SetMetadata("metersPerUnit", 1.0)

    # Warehouse root — declares the SimReady prop profile
    warehouse = UsdGeom.Xform.Define(stage, "/Warehouse")
    warehouse_api = UsdProfiles.ProfileAPI.Apply(warehouse.GetPrim())
    warehouse_api.GetProfileAttr().Set("com.nvidia.simready.prop_robotics_neutral")
    print(f"Applied profile to /Warehouse: com.nvidia.simready.prop_robotics_neutral")

    # Shelving unit — inherits the warehouse profile (no explicit profile)
    shelf = UsdGeom.Xform.Define(stage, "/Warehouse/ShelfUnit_A")
    UsdGeom.Xform.Define(stage, "/Warehouse/ShelfUnit_A/Frame")
    UsdGeom.Xform.Define(stage, "/Warehouse/ShelfUnit_A/Shelves")
    print(f"Defined /Warehouse/ShelfUnit_A (no explicit profile — inherits)")

    # Robot arm — declares its own, more specific profile
    robot = UsdGeom.Xform.Define(stage, "/Warehouse/RobotArm")
    robot_api = UsdProfiles.ProfileAPI.Apply(robot.GetPrim())
    robot_api.GetProfileAttr().Set("com.nvidia.simready.prop_robotics_neutral")
    # Also declare additional explicit capabilities
    robot_api.CreateCapabilitiesAttr([
        "com.nvidia.simready.physics.rigidBodies",
        "com.nvidia.simready.geom",
    ])
    print(f"Applied profile + explicit capabilities to /Warehouse/RobotArm")

    # Robot sub-components — inherit from the robot
    UsdGeom.Xform.Define(stage, "/Warehouse/RobotArm/Base")
    UsdGeom.Xform.Define(stage, "/Warehouse/RobotArm/Arm")
    UsdGeom.Xform.Define(stage, "/Warehouse/RobotArm/Gripper")
    print(f"Defined /Warehouse/RobotArm/{{Base,Arm,Gripper}} (inherit)")

    # ------------------------------------------------------------------
    # 3. Serialize to disk
    # ------------------------------------------------------------------
    print("\n--- 3. Serialize ---\n")

    stage.GetRootLayer().Save()
    print(f"Saved: warehouse.usda")
    print()

    # Print the USD file
    print(stage.GetRootLayer().ExportToString())

    # ------------------------------------------------------------------
    # 4. Query profiles — "nearest ancestor wins"
    # ------------------------------------------------------------------
    print("--- 4. Query: effective profiles ---\n")

    prims_to_query = [
        "/Warehouse",
        "/Warehouse/ShelfUnit_A",
        "/Warehouse/ShelfUnit_A/Frame",
        "/Warehouse/RobotArm",
        "/Warehouse/RobotArm/Gripper",
    ]

    for path in prims_to_query:
        prim = stage.GetPrimAtPath(path)
        profile = UsdProfiles.GetEffectiveProfile(prim)
        has_api = prim.HasAPI(UsdProfiles.ProfileAPI)
        source = "authored" if has_api else "inherited"
        print(f"  {path}")
        print(f"    profile: {profile} ({source})")

    # ------------------------------------------------------------------
    # 5. Query effective capabilities
    # ------------------------------------------------------------------
    print("\n--- 5. Query: effective capabilities ---\n")

    for path in ["/Warehouse", "/Warehouse/RobotArm"]:
        prim = stage.GetPrimAtPath(path)
        caps = UsdProfiles.GetEffectiveCapabilities(prim)
        print(f"  {path}")
        print(f"    capabilities: {[str(c) for c in caps]}")

    # ------------------------------------------------------------------
    # 6. Re-open and verify round-trip
    # ------------------------------------------------------------------
    print("\n--- 6. Round-trip: re-open from disk ---\n")

    stage2 = Usd.Stage.Open("warehouse.usda")
    robot2 = stage2.GetPrimAtPath("/Warehouse/RobotArm")
    profile2 = UsdProfiles.GetEffectiveProfile(robot2)
    caps2 = UsdProfiles.GetEffectiveCapabilities(robot2)
    print(f"  Re-opened warehouse.usda")
    print(f"  /Warehouse/RobotArm profile: {profile2}")
    print(f"  /Warehouse/RobotArm capabilities: {[str(c) for c in caps2]}")

    # Verify inheritance still works after round-trip
    gripper2 = stage2.GetPrimAtPath("/Warehouse/RobotArm/Gripper")
    gripper_profile = UsdProfiles.GetEffectiveProfile(gripper2)
    print(f"  /Warehouse/RobotArm/Gripper profile (inherited): {gripper_profile}")

    shelf_frame = stage2.GetPrimAtPath("/Warehouse/ShelfUnit_A/Frame")
    shelf_profile = UsdProfiles.GetEffectiveProfile(shelf_frame)
    print(f"  /Warehouse/ShelfUnit_A/Frame profile (inherited): {shelf_profile}")

    print()
    print("=" * 70)
    print("Done! The ProfileAPI enables declarative capability tagging at any")
    print("level of the prim hierarchy, with 'nearest ancestor wins' resolution.")
    print("=" * 70)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
test_fabric_population_units.py — Fabric-level tests for unit-corrected population.

These tests verify that the USDRT population plugin correctly applies unit
corrections when populating transforms into Fabric.

Run inside Kit headless:
    ./kit --exec test_fabric_population_units.py --/renderer/active="" --/app/fastShutdown=true

ALL TESTS SHOULD FAIL against the stock (unmodified) population plugin.
After our ConcurrentXformCache modification, they should pass.

Test matrix:
    1. test_basic_cm_to_m          — cm translate corrected in Fabric worldMatrix
    2. test_multi_scale            — cm, mm, m assets all correct in meters
    3. test_nested_hierarchy       — parent→child propagation
    4. test_identity_no_regression — same-unit prims unchanged
    5. test_resetXformStack        — resetXformStack + unit correction
    6. test_time_varying           — animated cm translate at two time codes
"""
import os
import sys
import time
import traceback

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

TOLERANCE = 1e-5
TESTENV = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'stages')
# All test assets (cm_asset.usda, mm_asset.usda, m_asset.usda) are in stages/
UNITS_TESTENV = TESTENV

_results = []


def _approx(a, b, tol=TOLERANCE):
    return abs(a - b) < tol


def _get_fabric_world_translate(fabric_stage, path):
    """Read world transform translation from Fabric via usdrt.Rt.Xformable."""
    import usdrt
    prim = fabric_stage.GetPrimAtPath(path)
    if not prim.IsValid():
        raise RuntimeError(f"Prim not found in Fabric: {path}")
    xf = usdrt.Rt.Xformable(prim)
    world = xf.GetWorldTransform()
    # Extract translation from row 3 (row-major GfMatrix4d)
    return (float(world[3][0]), float(world[3][1]), float(world[3][2]))


def _await_population():
    """Wait for Fabric population to complete."""
    import omni.kit.app
    # Wait several frames for population to finish
    for _ in range(60):
        omni.kit.app.get_app().update()
    time.sleep(0.5)
    for _ in range(10):
        omni.kit.app.get_app().update()


def _record(name, passed, msg=""):
    status = "PASS" if passed else "FAIL"
    _results.append((name, passed, msg))
    print(f"  [{status}] {name}" + (f" — {msg}" if msg else ""))


# ---------------------------------------------------------------------------
# Test 1: Basic cm→m
# ---------------------------------------------------------------------------
def test_basic_cm_to_m():
    """cm_asset.usda translate (100,200,300) should appear as (1,2,3) in meter Fabric."""
    import omni.usd
    import usdrt
    from pxr import Usd, UsdGeom, Sdf, Gf

    print("\n--- Test 1: Basic cm→m ---")

    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await_population()
    stage = ctx.get_stage()

    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    # Reference cm_asset.usda
    cm_asset = os.path.join(UNITS_TESTENV, 'cm_asset.usda')
    ref_prim = stage.DefinePrim('/World/CmRef')
    ref_prim.GetReferences().AddReference(cm_asset, '/Box')

    _await_population()

    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())
    tx, ty, tz = _get_fabric_world_translate(fabric_stage, '/World/CmRef')

    # With correction: (100,200,300) cm → (1,2,3) m
    # Without correction (stock): (100,200,300) — WRONG
    _record("basic_cm_translate_x", _approx(tx, 1.0),
            f"expected X≈1.0, got {tx:.6f}")
    _record("basic_cm_translate_y", _approx(ty, 2.0),
            f"expected Y≈2.0, got {ty:.6f}")
    _record("basic_cm_translate_z", _approx(tz, 3.0),
            f"expected Z≈3.0, got {tz:.6f}")


# ---------------------------------------------------------------------------
# Test 2: Multi-scale
# ---------------------------------------------------------------------------
def test_multi_scale():
    """cm, mm, and m assets referenced into a meter stage."""
    import omni.usd
    import usdrt
    from pxr import Usd, UsdGeom, Sdf, Gf

    print("\n--- Test 2: Multi-scale ---")

    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await_population()
    stage = ctx.get_stage()

    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    # cm: translate (100,200,300) → expect (1,2,3) in meters
    cm_ref = stage.DefinePrim('/World/CmRef')
    cm_ref.GetReferences().AddReference(
        os.path.join(UNITS_TESTENV, 'cm_asset.usda'), '/Box')

    # mm: translate (10000,5500,0) → expect (10,5.5,0) in meters
    mm_ref = stage.DefinePrim('/World/MmRef')
    mm_ref.GetReferences().AddReference(
        os.path.join(UNITS_TESTENV, 'mm_asset.usda'), '/Bolt')

    # m: translate (0,0,0) → expect (0,0,0) — no correction
    m_ref = stage.DefinePrim('/World/MRef')
    m_ref.GetReferences().AddReference(
        os.path.join(UNITS_TESTENV, 'm_asset.usda'), '/World')

    _await_population()

    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())

    # cm
    tx, ty, tz = _get_fabric_world_translate(fabric_stage, '/World/CmRef')
    _record("multi_cm_x", _approx(tx, 1.0), f"cm X: expected 1.0, got {tx:.6f}")
    _record("multi_cm_y", _approx(ty, 2.0), f"cm Y: expected 2.0, got {ty:.6f}")

    # mm
    tx, ty, tz = _get_fabric_world_translate(fabric_stage, '/World/MmRef')
    _record("multi_mm_x", _approx(tx, 10.0), f"mm X: expected 10.0, got {tx:.6f}")
    _record("multi_mm_y", _approx(ty, 5.5), f"mm Y: expected 5.5, got {ty:.6f}")

    # m (no correction needed)
    tx, ty, tz = _get_fabric_world_translate(fabric_stage, '/World/MRef')
    _record("multi_m_x", _approx(tx, 0.0), f"m X: expected 0.0, got {tx:.6f}")


# ---------------------------------------------------------------------------
# Test 3: Nested hierarchy
# ---------------------------------------------------------------------------
def test_nested_hierarchy():
    """Parent→child propagation: corrections compose through the hierarchy."""
    import omni.usd
    import usdrt
    from pxr import Usd, UsdGeom, Sdf, Gf

    print("\n--- Test 3: Nested hierarchy ---")

    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await_population()
    stage = ctx.get_stage()

    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    # Create anonymous cm layer with parent/child
    cm_layer = Sdf.Layer.CreateAnonymous('.usda')
    cm_layer.pseudoRoot.SetInfo('metersPerUnit', 0.01)

    with Sdf.ChangeBlock():
        parent_spec = Sdf.CreatePrimInLayer(cm_layer, '/CmRoot/Parent')
        parent_spec.typeName = 'Xform'
        parent_spec.specifier = Sdf.SpecifierDef
        a = Sdf.AttributeSpec(parent_spec, 'xformOp:translate',
                              Sdf.ValueTypeNames.Double3)
        a.default = Gf.Vec3d(100, 0, 0)  # 100cm = 1m
        o = Sdf.AttributeSpec(parent_spec, 'xformOpOrder',
                              Sdf.ValueTypeNames.TokenArray)
        o.default = ['xformOp:translate']

        child_spec = Sdf.CreatePrimInLayer(cm_layer, '/CmRoot/Parent/Child')
        child_spec.typeName = 'Xform'
        child_spec.specifier = Sdf.SpecifierDef
        a2 = Sdf.AttributeSpec(child_spec, 'xformOp:translate',
                               Sdf.ValueTypeNames.Double3)
        a2.default = Gf.Vec3d(50, 0, 0)  # 50cm = 0.5m relative to parent
        o2 = Sdf.AttributeSpec(child_spec, 'xformOpOrder',
                               Sdf.ValueTypeNames.TokenArray)
        o2.default = ['xformOp:translate']

        # Need the root prim too
        root_spec = Sdf.CreatePrimInLayer(cm_layer, '/CmRoot')
        root_spec.typeName = 'Xform'
        root_spec.specifier = Sdf.SpecifierDef

    ref_prim = stage.DefinePrim('/World/CmRef')
    ref_prim.GetReferences().AddReference(cm_layer.identifier, '/CmRoot')

    _await_population()

    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())

    # Parent: 100cm = 1m in world X
    px, _, _ = _get_fabric_world_translate(fabric_stage, '/World/CmRef/Parent')
    _record("nested_parent_x", _approx(px, 1.0),
            f"Parent X: expected 1.0, got {px:.6f}")

    # Child: parent (1m) + own (50cm=0.5m) = 1.5m in world X
    cx, _, _ = _get_fabric_world_translate(fabric_stage, '/World/CmRef/Parent/Child')
    _record("nested_child_x", _approx(cx, 1.5),
            f"Child X: expected 1.5, got {cx:.6f}")


# ---------------------------------------------------------------------------
# Test 4: Identity / no regression
# ---------------------------------------------------------------------------
def test_identity_no_regression():
    """Same-unit reference: transforms must be identical to stock population."""
    import omni.usd
    import usdrt
    from pxr import Usd, UsdGeom, Sdf, Gf

    print("\n--- Test 4: Identity / no regression ---")

    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await_population()
    stage = ctx.get_stage()

    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    # Native meter prim
    cube = UsdGeom.Xform.Define(stage, '/World/Cube')
    cube.AddTranslateOp().Set(Gf.Vec3d(5, 3, 1))

    # Meter-scale reference (same as stage — no correction needed)
    m_ref = stage.DefinePrim('/World/MRef')
    m_ref.GetReferences().AddReference(
        os.path.join(UNITS_TESTENV, 'm_asset.usda'), '/World')

    _await_population()

    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())

    # Native cube — must be exactly (5, 3, 1)
    tx, ty, tz = _get_fabric_world_translate(fabric_stage, '/World/Cube')
    _record("identity_cube_x", _approx(tx, 5.0),
            f"Cube X: expected 5.0, got {tx:.6f}")
    _record("identity_cube_y", _approx(ty, 3.0),
            f"Cube Y: expected 3.0, got {ty:.6f}")
    _record("identity_cube_z", _approx(tz, 1.0),
            f"Cube Z: expected 1.0, got {tz:.6f}")

    # Meter-scale ref — must be exactly (0, 0, 0)
    tx, ty, tz = _get_fabric_world_translate(fabric_stage, '/World/MRef')
    _record("identity_mref_x", _approx(tx, 0.0),
            f"MRef X: expected 0.0, got {tx:.6f}")


# ---------------------------------------------------------------------------
# Test 5: resetXformStack
# ---------------------------------------------------------------------------
def test_resetXformStack():
    """resetXformStack + unit correction: child ignores parent, keeps own correction."""
    import omni.usd
    import usdrt
    from pxr import Usd, UsdGeom, Sdf, Gf

    print("\n--- Test 5: resetXformStack ---")

    ctx = omni.usd.get_context()
    stage_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        'stages', 'test_resetXformStack.usda')
    ctx.open_stage(stage_path)
    _await_population()

    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())

    # Parent: 100cm → 1m
    px, _, _ = _get_fabric_world_translate(fabric_stage, '/World/CmRef/Parent')
    _record("reset_parent_x", _approx(px, 1.0),
            f"Parent X: expected 1.0, got {px:.6f}")

    # ResetChild: !resetXformStack! + translate (200,0,0) in cm → 2m world
    # It ignores the parent transform entirely.
    rx, _, _ = _get_fabric_world_translate(
        fabric_stage, '/World/CmRef/Parent/ResetChild')
    _record("reset_child_x", _approx(rx, 2.0),
            f"ResetChild X: expected 2.0, got {rx:.6f}")


# ---------------------------------------------------------------------------
# Test 6: Time-varying
# ---------------------------------------------------------------------------
def test_time_varying():
    """Animated cm translate at two time codes."""
    import omni.usd
    import usdrt
    from pxr import Usd, UsdGeom, Sdf, Gf

    print("\n--- Test 6: Time-varying ---")

    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await_population()
    stage = ctx.get_stage()

    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    anim_asset = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        'stages', 'cm_animated_asset.usda')
    ref_prim = stage.DefinePrim('/World/CmAnim')
    ref_prim.GetReferences().AddReference(anim_asset, '/CmAnim')

    # Frame 1: translate (0,0,0)
    import omni.timeline
    timeline = omni.timeline.get_timeline_interface()
    timeline.set_current_time(1.0 / stage.GetFramesPerSecond())
    _await_population()

    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())
    tx1, _, _ = _get_fabric_world_translate(fabric_stage, '/World/CmAnim')
    _record("timevar_t1_x", _approx(tx1, 0.0),
            f"t=1 X: expected 0.0, got {tx1:.6f}")

    # Frame 24: translate (200,0,0) cm → 2m
    timeline.set_current_time(24.0 / stage.GetFramesPerSecond())
    _await_population()

    tx24, _, _ = _get_fabric_world_translate(fabric_stage, '/World/CmAnim')
    _record("timevar_t24_x", _approx(tx24, 2.0),
            f"t=24 X: expected 2.0, got {tx24:.6f}")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------
def main():
    print("=" * 60)
    print("FABRIC POPULATION UNITS — TEST SUITE")
    print("=" * 60)
    print(f"Test stages dir: {TESTENV}")
    print(f"POC assets dir:  {UNITS_TESTENV}")

    tests = [
        test_basic_cm_to_m,
        test_multi_scale,
        test_nested_hierarchy,
        test_identity_no_regression,
        test_resetXformStack,
        test_time_varying,
    ]

    for test_fn in tests:
        try:
            test_fn()
        except Exception as e:
            _record(test_fn.__name__, False, f"EXCEPTION: {e}")
            traceback.print_exc()

    # Summary
    print("\n" + "=" * 60)
    passed = sum(1 for _, p, _ in _results if p)
    failed = sum(1 for _, p, _ in _results if not p)
    print(f"RESULTS: {passed} passed, {failed} failed, {len(_results)} total")

    if failed > 0:
        print("\nFAILED:")
        for name, p, msg in _results:
            if not p:
                print(f"  ✗ {name}: {msg}")

    print("=" * 60)

    # Exit Kit
    import omni.kit.app
    omni.kit.app.get_app().post_quit()

    return 1 if failed > 0 else 0


main()

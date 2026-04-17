"""Fabric Population Units Test — uses correct USDRT API"""
import omni.kit.app
import omni.usd
import usdrt
from pxr import Usd, UsdGeom, Sdf, Gf
import time, os, traceback

TOLERANCE = 1e-5
TESTENV = '/home/horde/OpenUSD/extras/fabricUnitsResolution/test/stages'
_results = []

def _approx(a, b, tol=TOLERANCE):
    return abs(a - b) < tol

def _await():
    for _ in range(60):
        omni.kit.app.get_app().update()
    time.sleep(0.5)
    for _ in range(10):
        omni.kit.app.get_app().update()

def _get_fabric_translate(ctx, path):
    fabric_stage = usdrt.Usd.Stage.Attach(ctx.get_stage_id())
    prim = fabric_stage.GetPrimAtPath(path)
    if not prim.IsValid():
        raise RuntimeError(f"Prim not found: {path}")
    xf = usdrt.Rt.Xformable(prim)
    mat = xf.GetFabricHierarchyWorldMatrixAttr().Get()
    return (float(mat[3][0]), float(mat[3][1]), float(mat[3][2]))

def _record(name, passed, msg=""):
    _results.append((name, passed, msg))
    print(f"  [{'PASS' if passed else 'FAIL'}] {name}" + (f" — {msg}" if msg else ""))

# Test 1: Basic cm→m
def test_basic_cm_to_m():
    print("\n--- Test 1: Basic cm→m ---")
    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await()
    stage = ctx.get_stage()
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    ref_prim = stage.DefinePrim('/World/CmRef')
    ref_prim.GetReferences().AddReference(os.path.join(TESTENV, 'cm_asset.usda'), '/Box')
    _await()
    tx, ty, tz = _get_fabric_translate(ctx, '/World/CmRef')
    _record("cm_x", _approx(tx, 1.0), f"expected 1.0, got {tx:.6f}")
    _record("cm_y", _approx(ty, 2.0), f"expected 2.0, got {ty:.6f}")
    _record("cm_z", _approx(tz, 3.0), f"expected 3.0, got {tz:.6f}")

# Test 2: Multi-scale
def test_multi_scale():
    print("\n--- Test 2: Multi-scale ---")
    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await()
    stage = ctx.get_stage()
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    cm = stage.DefinePrim('/World/CmRef')
    cm.GetReferences().AddReference(os.path.join(TESTENV, 'cm_asset.usda'), '/Box')
    mm = stage.DefinePrim('/World/MmRef')
    mm.GetReferences().AddReference(os.path.join(TESTENV, 'mm_asset.usda'), '/Bolt')
    m = stage.DefinePrim('/World/MRef')
    m.GetReferences().AddReference(os.path.join(TESTENV, 'm_asset.usda'), '/World')
    _await()
    tx, ty, _ = _get_fabric_translate(ctx, '/World/CmRef')
    _record("multi_cm_x", _approx(tx, 1.0), f"cm X: expected 1.0, got {tx:.6f}")
    _record("multi_cm_y", _approx(ty, 2.0), f"cm Y: expected 2.0, got {ty:.6f}")
    tx, ty, _ = _get_fabric_translate(ctx, '/World/MmRef')
    _record("multi_mm_x", _approx(tx, 10.0), f"mm X: expected 10.0, got {tx:.6f}")
    _record("multi_mm_y", _approx(ty, 5.5), f"mm Y: expected 5.5, got {ty:.6f}")
    tx, _, _ = _get_fabric_translate(ctx, '/World/MRef')
    _record("multi_m_x", _approx(tx, 0.0), f"m X: expected 0.0, got {tx:.6f}")

# Test 3: Identity (no regression)
def test_identity():
    print("\n--- Test 3: Identity / no regression ---")
    ctx = omni.usd.get_context()
    ctx.new_stage()
    _await()
    stage = ctx.get_stage()
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    cube = UsdGeom.Xform.Define(stage, '/World/Cube')
    cube.AddTranslateOp().Set(Gf.Vec3d(5, 3, 1))
    _await()
    tx, ty, tz = _get_fabric_translate(ctx, '/World/Cube')
    _record("identity_x", _approx(tx, 5.0), f"expected 5.0, got {tx:.6f}")
    _record("identity_y", _approx(ty, 3.0), f"expected 3.0, got {ty:.6f}")
    _record("identity_z", _approx(tz, 1.0), f"expected 1.0, got {tz:.6f}")

# Runner
print("=" * 60)
print("FABRIC POPULATION UNITS — TEST SUITE (fixed API)")
print("=" * 60)

tests = [test_basic_cm_to_m, test_multi_scale, test_identity]
for test_fn in tests:
    try:
        test_fn()
    except Exception as e:
        _record(test_fn.__name__, False, f"EXCEPTION: {e}")
        traceback.print_exc()

print("\n" + "=" * 60)
passed = sum(1 for _, p, _ in _results if p)
failed = sum(1 for _, p, _ in _results if not p)
print(f"RESULTS: {passed} passed, {failed} failed, {len(_results)} total")
if failed:
    print("\nFAILED:")
    for n, p, m in _results:
        if not p:
            print(f"  ✗ {n}: {m}")
print("=" * 60)

omni.kit.app.get_app().post_quit()

"""Test the Newton GPU axis convention and coordinate transforms.

Validates:
1. Newton GPU's internal coordinate system (Z-up vs Y-up)
2. The quaternion-based coordinate conversion for Y-up stages
3. That transform writeback produces correct USD-space positions
4. That a camera ray aimed at a body hits after coordinate conversion
5. That the full round-trip (USD → Newton → sim → USD) is correct

Run: python3 test_axis_and_grab.py
"""
import sys, os, math
sys.path.insert(0, "/home/horde/.openclaw/workspace-newton-usd/usd_build/lib/python")
os.environ["LD_LIBRARY_PATH"] = "/home/horde/.openclaw/workspace-newton-usd/usd_build/lib"

from pxr import Usd, UsdGeom, UsdPhysics, Sdf, Gf
import newton
import warp as wp
import numpy as np

PASS = 0
FAIL = 0

def check(name, condition, detail=""):
    global PASS, FAIL
    if condition:
        print(f"  ✅ {name}")
        PASS += 1
    else:
        print(f"  ❌ {name}: {detail}")
        FAIL += 1

SCENE = "/home/horde/.openclaw/workspace-newton-usd/OpenUSD/extras/exec/examples/newtonPhysics/testenv/fallingBox.usda"

# =========================================================================
# Test 1: Newton GPU axis convention discovery
# =========================================================================
print("=== Test 1: Axis convention ===")

stage = Usd.Stage.Open(SCENE)
up_axis = UsdGeom.GetStageUpAxis(stage)
check("Stage is Y-up", up_axis == UsdGeom.Tokens.y)

usd_translate = stage.GetPrimAtPath("/World/FallingBox").GetAttribute("xformOp:translate").Get()
check("Box at (0, 10, 0) in USD", 
      abs(usd_translate[1] - 10) < 0.1 and abs(usd_translate[0]) < 0.1 and abs(usd_translate[2]) < 0.1)

builder = newton.ModelBuilder()
builder.add_usd(stage)
model = builder.finalize(device="cuda:0")
state = model.state()
bq = state.body_q.numpy()[0]

newton_pos = bq[:3]
check("Newton is Z-up: box at (0, ~0, 10)",
      abs(newton_pos[2] - 10) < 0.5 and abs(newton_pos[1]) < 0.5,
      f"newton_pos=({newton_pos[0]:.2f}, {newton_pos[1]:.2f}, {newton_pos[2]:.2f})")

newton_quat = bq[3:7]  # qx, qy, qz, qw
check("Newton applies +90° X rotation (qx≈0.7071, qw≈0.7071)",
      abs(newton_quat[0] - 0.7071) < 0.01 and abs(newton_quat[3] - 0.7071) < 0.01,
      f"quat=({newton_quat[0]:.4f}, {newton_quat[1]:.4f}, {newton_quat[2]:.4f}, {newton_quat[3]:.4f})")

# =========================================================================
# Test 2: Coordinate conversion (Newton Z-up → USD Y-up)
# =========================================================================
print("\n=== Test 2: Coordinate conversion ===")

# The undo quaternion: -90° around X
q_undo = Gf.Quatd(math.cos(-math.pi/4), math.sin(-math.pi/4), 0, 0)

# Convert Newton transform to USD
q_newton = Gf.Quatd(float(bq[6]), float(bq[3]), float(bq[4]), float(bq[5]))
pos_newton = Gf.Vec3d(float(bq[0]), float(bq[1]), float(bq[2]))

q_usd = q_undo * q_newton
pos_usd = q_undo.Transform(pos_newton)

check("USD position ≈ (0, 10, 0)",
      abs(pos_usd[0]) < 0.1 and abs(pos_usd[1] - 10) < 0.5 and abs(pos_usd[2]) < 0.1,
      f"pos_usd=({pos_usd[0]:.2f}, {pos_usd[1]:.2f}, {pos_usd[2]:.2f})")

rot_usd = Gf.Rotation(q_usd)
check("USD rotation ≈ identity (angle < 1°)",
      rot_usd.GetAngle() < 1.0,
      f"angle={rot_usd.GetAngle():.2f}°")

# =========================================================================
# Test 3: Simulation round-trip
# =========================================================================
print("\n=== Test 3: Simulation round-trip ===")

solver = newton.solvers.SolverXPBD(model)
s0 = model.state()
s1 = model.state()
for i in range(60):
    contacts = model.collide(s0)
    solver.step(s0, s1, control=None, contacts=contacts, dt=1/24.0)
    s0, s1 = s1, s0

bq_after = s0.body_q.numpy()[0]
q_n_after = Gf.Quatd(float(bq_after[6]), float(bq_after[3]), float(bq_after[4]), float(bq_after[5]))
pos_n_after = Gf.Vec3d(float(bq_after[0]), float(bq_after[1]), float(bq_after[2]))

pos_usd_after = q_undo.Transform(pos_n_after)
check("Box fell in USD Y (Y decreased)",
      pos_usd_after[1] < usd_translate[1] - 1.0,
      f"Y: {usd_translate[1]:.1f} → {pos_usd_after[1]:.2f}")

check("Box stayed in XZ plane (X ≈ 0, Z ≈ 0)",
      abs(pos_usd_after[0]) < 1.0 and abs(pos_usd_after[2]) < 1.0,
      f"X={pos_usd_after[0]:.2f}, Z={pos_usd_after[2]:.2f}")

# =========================================================================
# Test 4: Grab raycast
# =========================================================================
print("\n=== Test 4: Grab raycast ===")

# The forward conversion: USD Y-up → Newton Z-up (+90° around X)
q_to_newton = Gf.Quatd(math.cos(math.pi/4), math.sin(math.pi/4), 0, 0)

# Camera at USD (0, 10, 20) looking toward (0, 10, 0) → dir (0, 0, -1)
cam_o_usd = Gf.Vec3d(0, 10, 20)
cam_d_usd = Gf.Vec3d(0, 0, -1)

cam_o_newton = q_to_newton.Transform(cam_o_usd)
cam_d_newton = q_to_newton.Transform(cam_d_usd)

print(f"  USD ray: o=({cam_o_usd[0]:.1f},{cam_o_usd[1]:.1f},{cam_o_usd[2]:.1f}) d=({cam_d_usd[0]:.1f},{cam_d_usd[1]:.1f},{cam_d_usd[2]:.1f})")
print(f"  Newton ray: o=({cam_o_newton[0]:.1f},{cam_o_newton[1]:.1f},{cam_o_newton[2]:.1f}) d=({cam_d_newton[0]:.1f},{cam_d_newton[1]:.1f},{cam_d_newton[2]:.1f})")

# In Newton Z-up: box at (0, 0, 10), camera should be at (0, -20, 10) looking (0, 1, 0)
check("Newton camera Z ≈ 10 (box height in Z-up)",
      abs(cam_o_newton[2] - 10) < 0.5,
      f"z={cam_o_newton[2]:.2f}")
check("Newton camera dir points toward body",
      abs(cam_d_newton[1] - 1.0) < 0.1 or abs(cam_d_newton[1] + 1.0) < 0.1,
      f"dir_y={cam_d_newton[1]:.4f}")

# Actual raycast
fresh_state = model.state()
picking = newton._src.viewer.picking.Picking(model)
picking.pick(
    fresh_state,
    wp.vec3f(float(cam_o_newton[0]), float(cam_o_newton[1]), float(cam_o_newton[2])),
    wp.vec3f(float(cam_d_newton[0]), float(cam_d_newton[1]), float(cam_d_newton[2])))

check("Raycast hits the box", picking.is_picking(), "ray missed")
if picking.is_picking():
    picking.release()

# Test a ray that should MISS (pointing away from the body)
cam_d_miss = Gf.Vec3d(0, 0, 1)  # pointing away in USD
cam_d_miss_n = q_to_newton.Transform(cam_d_miss)
fresh_state2 = model.state()
picking2 = newton._src.viewer.picking.Picking(model)
picking2.pick(
    fresh_state2,
    wp.vec3f(float(cam_o_newton[0]), float(cam_o_newton[1]), float(cam_o_newton[2])),
    wp.vec3f(float(cam_d_miss_n[0]), float(cam_d_miss_n[1]), float(cam_d_miss_n[2])))
check("Opposite-direction ray misses", not picking2.is_picking(), "ray hit when it shouldn't")

# =========================================================================
# Test 5: Matrix construction matches expected USD transform
# =========================================================================
print("\n=== Test 5: Full matrix construction ===")

# Build the GfMatrix4d the same way the plugin does
q_usd_full = q_undo * q_newton
pos_usd_full = q_undo.Transform(pos_newton)
mat = Gf.Matrix4d()
mat.SetRotate(q_usd_full)
mat.SetTranslateOnly(pos_usd_full)

# The matrix should be approximately identity rotation + translate (0, 10, 0)
expected = Gf.Matrix4d()
expected.SetTranslateOnly(Gf.Vec3d(0, 10, 0))
diff = 0.0
for r in range(4):
    for c in range(4):
        diff += abs(mat[r][c] - expected[r][c])
check("Matrix ≈ translate(0,10,0)", diff < 0.1, f"diff={diff:.4f}")

# =========================================================================
# Summary
# =========================================================================
print(f"\n{'='*50}")
print(f"Results: {PASS} passed, {FAIL} failed")
if FAIL == 0:
    print("All tests passed! ✅")
else:
    print(f"⚠️  {FAIL} test(s) failed")
    sys.exit(1)

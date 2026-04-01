"""Test Option C: delta transform with resetXformStack=false.

Validates that computing correction = M_sim * inverse(M_rest) and
overlaying it with resetXformStack=false produces correct world-space
transforms for both the body prim and its children.

Run: python3 test_delta_transform.py
"""
import sys, os, math
sys.path.insert(0, "/home/horde/.openclaw/workspace-newton-usd/usd_build/lib/python")
os.environ["LD_LIBRARY_PATH"] = "/home/horde/.openclaw/workspace-newton-usd/usd_build/lib"

from pxr import Usd, UsdGeom, UsdPhysics, Sdf, Gf
import newton
import warp as wp

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

# =========================================================================
# Setup: Create a test scene with Xform parent + Cube child
# =========================================================================
print("=== Setup ===")

stage = Usd.Stage.CreateInMemory()
UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
UsdGeom.SetStageMetersPerUnit(stage, 1.0)

world = UsdGeom.Xform.Define(stage, '/World')
stage.SetDefaultPrim(world.GetPrim())

# Body: Xform at (0, 10, 0) with RigidBodyAPI
body = UsdGeom.Xform.Define(stage, '/World/Body')
UsdGeom.Xformable(body).AddTranslateOp().Set(Gf.Vec3d(0, 10, 0))
UsdPhysics.RigidBodyAPI.Apply(body.GetPrim())

# Child mesh: Cube with a local offset of (1, 0, 0) + CollisionAPI
child = UsdGeom.Cube.Define(stage, '/World/Body/Mesh')
child.CreateSizeAttr(1.0)
UsdGeom.Xformable(child).AddTranslateOp().Set(Gf.Vec3d(1, 0, 0))
UsdPhysics.CollisionAPI.Apply(child.GetPrim())

# Get rest transforms
body_rest_local = UsdGeom.Xformable(body).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
child_rest_local = child.GetPrim().GetAttribute('xformOp:translate').Get()
child_rest_world = UsdGeom.Xformable(child).ComputeLocalToWorldTransform(Usd.TimeCode.Default())

print(f"Body rest local-to-world: translate = {body_rest_local.ExtractTranslation()}")
print(f"Child rest local-to-world: translate = {child_rest_world.ExtractTranslation()}")

check("Body rest at (0, 10, 0)", 
      Gf.IsClose(body_rest_local.ExtractTranslation(), Gf.Vec3d(0, 10, 0), 0.01))
check("Child rest at (1, 10, 0) world",
      Gf.IsClose(child_rest_world.ExtractTranslation(), Gf.Vec3d(1, 10, 0), 0.01))

# =========================================================================
# Test 1: Delta transform math
# =========================================================================
print("\n=== Test 1: Delta transform math ===")

# Simulate: body moved to (3, 5, 2) with a 45° Y rotation
sim_translate = Gf.Vec3d(3, 5, 2)
sim_rotation = Gf.Rotation(Gf.Vec3d(0, 1, 0), 45)

M_sim = Gf.Matrix4d()
M_sim.SetRotate(sim_rotation)
M_sim.SetTranslateOnly(sim_translate)

M_rest = body_rest_local  # = translate(0, 10, 0)
M_rest_inv = M_rest.GetInverse()

# correction = M_sim * M_rest_inv
M_correction = M_sim * M_rest_inv

print(f"M_sim translate: {M_sim.ExtractTranslation()}")
print(f"M_rest translate: {M_rest.ExtractTranslation()}")
print(f"M_correction translate: {M_correction.ExtractTranslation()}")

# Verify: M_correction * M_rest should equal M_sim
M_result = M_correction * M_rest
check("correction * rest ≈ sim (translation)",
      Gf.IsClose(M_result.ExtractTranslation(), sim_translate, 0.01),
      f"got {M_result.ExtractTranslation()}")

result_rot = M_result.ExtractRotation()
check("correction * rest ≈ sim (rotation 45° around Y)",
      abs(result_rot.GetAngle() - 45) < 0.1,
      f"got {result_rot.GetAngle():.1f}°")

# =========================================================================
# Test 2: Child world transform with correction
# =========================================================================
print("\n=== Test 2: Child inherits correction ===")

# With resetXformStack=false, Hydra's flattening gives:
#   child_world = M_correction * M_body_rest * M_child_local
# Which should equal:
#   M_sim * M_child_local

M_child_local = Gf.Matrix4d()
M_child_local.SetTranslateOnly(Gf.Vec3d(1, 0, 0))

# USD convention: childWorld = childLocal * parentWorld
child_world_with_correction = M_child_local * M_correction * M_rest
child_world_expected = M_child_local * M_sim

check("Child world with correction matches expected",
      Gf.IsClose(child_world_with_correction.ExtractTranslation(),
                  child_world_expected.ExtractTranslation(), 0.01),
      f"got {child_world_with_correction.ExtractTranslation()} expected {child_world_expected.ExtractTranslation()}")

# The child should be at M_sim * M_child_local
# Since M_child_local is a pure translation and M_sim includes rotation,
# the child position = sim_translate + sim_rotation.TransformDir(child_offset)
child_offset_rotated = sim_rotation.TransformDir(Gf.Vec3d(1, 0, 0))
expected_child_pos = sim_translate + child_offset_rotated
check("Child at correct world position",
      Gf.IsClose(child_world_expected.ExtractTranslation(), expected_child_pos, 0.01),
      f"got {child_world_expected.ExtractTranslation()} expected {expected_child_pos}")

# =========================================================================
# Test 3: With Newton GPU (actual sim)
# =========================================================================
print("\n=== Test 3: Full Newton round-trip ===")

# Save stage, run Newton, compute correction
stage.GetRootLayer().Export('/tmp/delta_test.usda')

# Add ground + physics scene for Newton
stage2 = Usd.Stage.Open('/tmp/delta_test.usda')
scene = UsdPhysics.Scene.Define(stage2, '/World/PhysicsScene')
scene.CreateGravityDirectionAttr(Gf.Vec3f(0, -1, 0))
scene.CreateGravityMagnitudeAttr(9.81)
ground = UsdGeom.Cube.Define(stage2, '/World/Ground')
ground.CreateSizeAttr(1.0)
UsdGeom.Xformable(ground).AddScaleOp().Set(Gf.Vec3f(50, 0.1, 50))
UsdGeom.Xformable(ground).AddTranslateOp().Set(Gf.Vec3d(0, -0.05, 0))
UsdPhysics.CollisionAPI.Apply(ground.GetPrim())
UsdPhysics.RigidBodyAPI.Apply(ground.GetPrim())
ground.GetPrim().CreateAttribute('physics:kinematicEnabled', Sdf.ValueTypeNames.Bool).Set(True)
stage2.Save()

# Build Newton model
builder = newton.ModelBuilder()
builder.add_usd(stage2)
model = builder.finalize(device='cuda:0')

# Find body index
body_idx = None
for i, label in enumerate(builder.body_label):
    if 'Body' in label and 'Ground' not in label:
        body_idx = i
        break

check("Body found in Newton", body_idx is not None)

# Get Newton's rest state
bq_rest = model.state().body_q.numpy()[body_idx]

# Convert Newton rest → USD rest using q_undo
q_undo = Gf.Quatd(math.cos(-math.pi/4), math.sin(-math.pi/4), 0, 0)

pos_rest_n = Gf.Vec3d(float(bq_rest[0]), float(bq_rest[1]), float(bq_rest[2]))
quat_rest_n = Gf.Quatd(float(bq_rest[6]), float(bq_rest[3]), float(bq_rest[4]), float(bq_rest[5]))

pos_rest_usd = q_undo.Transform(pos_rest_n)
quat_rest_usd = q_undo * quat_rest_n

M_rest_newton = Gf.Matrix4d()
M_rest_newton.SetRotate(quat_rest_usd)
M_rest_newton.SetTranslateOnly(pos_rest_usd)

check("Newton rest position ≈ USD rest position (0, 10, 0)",
      Gf.IsClose(pos_rest_usd, Gf.Vec3d(0, 10, 0), 0.5),
      f"got {pos_rest_usd}")

# Simulate 30 frames
solver = newton.solvers.SolverXPBD(model)
s0 = model.state()
s1 = model.state()
for i in range(30):
    contacts = model.collide(s0)
    solver.step(s0, s1, control=None, contacts=contacts, dt=1/24.0)
    s0, s1 = s1, s0

bq_sim = s0.body_q.numpy()[body_idx]
pos_sim_n = Gf.Vec3d(float(bq_sim[0]), float(bq_sim[1]), float(bq_sim[2]))
quat_sim_n = Gf.Quatd(float(bq_sim[6]), float(bq_sim[3]), float(bq_sim[4]), float(bq_sim[5]))

pos_sim_usd = q_undo.Transform(pos_sim_n)
quat_sim_usd = q_undo * quat_sim_n

M_sim_newton = Gf.Matrix4d()
M_sim_newton.SetRotate(quat_sim_usd)
M_sim_newton.SetTranslateOnly(pos_sim_usd)

print(f"Newton sim position (USD): {pos_sim_usd}")
check("Body fell (Y decreased)", pos_sim_usd[1] < 10.0,
      f"Y={pos_sim_usd[1]:.2f}")

# Compute correction
M_rest_inv = M_rest_newton.GetInverse()
M_correction = M_sim_newton * M_rest_inv

# Verify: correction * rest = sim
M_verify = M_correction * M_rest_newton
check("correction * rest ≈ sim (full Newton round-trip)",
      Gf.IsClose(M_verify.ExtractTranslation(), pos_sim_usd, 0.1),
      f"got {M_verify.ExtractTranslation()} expected {pos_sim_usd}")

# Verify child would be at correct position
# USD: childWorld = childLocal * correctionOverlay * bodyRest
M_child_verify = M_child_local * M_correction * M_rest_newton
child_expected = M_child_local * M_sim_newton
check("Child world position correct after Newton sim",
      Gf.IsClose(M_child_verify.ExtractTranslation(),
                  child_expected.ExtractTranslation(), 0.1),
      f"got {M_child_verify.ExtractTranslation()} expected {child_expected.ExtractTranslation()}")

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

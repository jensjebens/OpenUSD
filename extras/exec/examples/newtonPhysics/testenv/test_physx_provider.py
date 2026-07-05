"""
Test suite for PhysXProvider subprocess-based physics simulation.

Exercises:
1. Worker init (launches subprocess, gets body count and prim_paths)
2. Step + pose readback (bodies fall under gravity, Y decreases)
3. Grab (move a body to a target position, verify it moved)
4. Release/cleanup (no leaked shm segments)

Run with:
  PYTHONPATH=/home/horde/.openclaw/workspace-reparent/OpenUSD/extras/exec/examples/newtonPhysics:/home/horde/newton-hydra-install/lib/python/ \
  /home/horde/newton-hydra-venv/bin/python3 -m pytest test_physx_provider.py -v

Or standalone:
  PYTHONPATH=... /home/horde/newton-hydra-venv/bin/python3 test_physx_provider.py
"""

import sys
import os
import time

# Ensure correct paths for imports
_newton_physics_dir = "/home/horde/.openclaw/workspace-reparent/OpenUSD/extras/exec/examples/newtonPhysics"
_pxr_lib_dir = "/home/horde/newton-hydra-install/lib/python/"

for p in (_newton_physics_dir, _pxr_lib_dir):
    if p not in sys.path:
        sys.path.insert(0, p)

from pxr import Usd, Sdf, Gf, UsdGeom
from usdviewPlugin import PhysXProvider

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SCENE_PATH = "/tmp/stacked_boxes_spaced.usda"
EXPECTED_BODIES = 3  # Box1, Box2, Box3 (Ground has no RigidBodyAPI)
STEP_DT = 1.0 / 60.0  # 60Hz physics

# Initial Y positions from the USDA
INITIAL_Y = {
    "/World/Box1": 0.6,
    "/World/Box2": 1.7,
    "/World/Box3": 2.8,
}


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _create_provider():
    """Create and initialize a PhysXProvider with the stacked boxes scene."""
    stage = Usd.Stage.Open(SCENE_PATH)
    provider = PhysXProvider()
    body_map = provider.initialize(SCENE_PATH, stage, device='gpu')
    return provider, body_map, stage


def _get_y_positions(provider):
    """Return dict of {prim_path_str: Y_position} from current poses."""
    transforms = provider.get_body_transforms(swap_yz=False)
    positions = {}
    for path, mat in transforms:
        translate = mat.ExtractTranslation()
        positions[str(path)] = translate[1]
    return positions


def _shm_exists(name):
    """Check if a shared memory segment exists."""
    try:
        from multiprocessing import shared_memory
        shm = shared_memory.SharedMemory(name=name, create=False)
        shm.close()
        return True
    except FileNotFoundError:
        return False


# ---------------------------------------------------------------------------
# Test 1: Worker Init
# ---------------------------------------------------------------------------

def test_worker_init():
    """Worker launches subprocess, returns correct body count and prim paths."""
    t0 = time.perf_counter()

    provider, body_map, stage = _create_provider()

    try:
        init_time = time.perf_counter() - t0
        print(f"\n  [timing] Worker init: {init_time:.3f}s")

        # Correct number of bodies (3 rigid body boxes, ground is static-only)
        assert len(body_map) == EXPECTED_BODIES, (
            f"Expected {EXPECTED_BODIES} bodies, got {len(body_map)}")

        # All expected paths present
        for path_str in INITIAL_Y:
            sdf_path = Sdf.Path(path_str)
            assert sdf_path in body_map, f"Missing body: {path_str}"

        # Body indices are 0-based integers
        indices = sorted(body_map.values())
        assert indices == list(range(EXPECTED_BODIES)), (
            f"Body indices should be 0..{EXPECTED_BODIES-1}, got {indices}")

        # Shared memory segments should exist
        assert _shm_exists("ovphysx_ctrl"), "Control shm not found"
        assert _shm_exists("ovphysx_poses"), "Pose shm not found"

        print(f"  [result] {len(body_map)} bodies: {[str(p) for p in body_map.keys()]}")

    finally:
        provider.release()


# ---------------------------------------------------------------------------
# Test 2: Step + Pose Readback (gravity)
# ---------------------------------------------------------------------------

def test_step_and_gravity():
    """After stepping simulation, bodies fall under gravity (Y decreases)."""
    provider, body_map, stage = _create_provider()

    try:
        # Read initial poses (should be near authored positions)
        # Step once to let initial poses populate
        provider.step(STEP_DT, 0.0)
        time.sleep(0.05)  # Let worker complete first step

        initial_y = _get_y_positions(provider)
        print(f"\n  [initial] Y positions: { {k: f'{v:.3f}' for k, v in initial_y.items()} }")

        # Step simulation for ~1 second (60 frames at 60Hz)
        t0 = time.perf_counter()
        num_steps = 60
        for i in range(num_steps):
            provider.step(STEP_DT, (i + 1) * STEP_DT)
            time.sleep(0.005)  # 5ms between steps — give worker time

        # Wait a bit more for the last step to finish
        time.sleep(0.05)
        step_time = time.perf_counter() - t0
        print(f"  [timing] {num_steps} steps in {step_time:.3f}s "
              f"({num_steps/step_time:.1f} steps/s)")

        # Read final poses
        final_y = _get_y_positions(provider)
        print(f"  [final]   Y positions: { {k: f'{v:.3f}' for k, v in final_y.items()} }")

        # All bodies should have fallen (Y decreased)
        for path_str in INITIAL_Y:
            y_init = initial_y[path_str]
            y_final = final_y[path_str]
            # Under gravity for 1s from rest: should fall significantly
            # Even with ground collision, the top box should have moved down
            assert y_final < y_init, (
                f"{path_str}: Y should decrease under gravity. "
                f"Initial={y_init:.3f}, Final={y_final:.3f}")

        # Verify they haven't fallen to -infinity (ground should stop them)
        for path_str, y_val in final_y.items():
            assert y_val > -10.0, (
                f"{path_str}: Y={y_val:.3f} is too low — ground should stop fall")

    finally:
        provider.release()


# ---------------------------------------------------------------------------
# Test 3: Grab (move body to target position)
# ---------------------------------------------------------------------------

def test_grab_moves_body():
    """Grabbing a body and setting a target position moves it there."""
    provider, body_map, stage = _create_provider()

    try:
        # Let simulation settle briefly
        for i in range(10):
            provider.step(STEP_DT, i * STEP_DT)
            time.sleep(0.005)
        time.sleep(0.05)

        # Get the index of Box3 (top box)
        box3_path = Sdf.Path("/World/Box3")
        box3_idx = body_map[box3_path]

        # Read position before grab
        transforms_before = provider.get_body_transforms(swap_yz=False)
        pos_before = None
        for path, mat in transforms_before:
            if path == box3_path:
                pos_before = mat.ExtractTranslation()
                break
        assert pos_before is not None, "Could not find Box3 in transforms"
        print(f"\n  [before grab] Box3 at ({pos_before[0]:.3f}, {pos_before[1]:.3f}, {pos_before[2]:.3f})")

        # Set grab target: move Box3 to (5, 5, 0)
        target = (5.0, 5.0, 0.0)
        provider.begin_grab(box3_idx, target)

        # Step while grabbing — the velocity-based grab should pull the body
        t0 = time.perf_counter()
        for i in range(120):
            provider.update_grab(target)
            provider.step(STEP_DT, (10 + i) * STEP_DT)
            time.sleep(0.005)
        time.sleep(0.05)
        grab_time = time.perf_counter() - t0
        print(f"  [timing] 120 grab steps in {grab_time:.3f}s")

        # Read position after grab
        transforms_after = provider.get_body_transforms(swap_yz=False)
        pos_after = None
        for path, mat in transforms_after:
            if path == box3_path:
                pos_after = mat.ExtractTranslation()
                break
        assert pos_after is not None, "Could not find Box3 in transforms after grab"
        print(f"  [after grab]  Box3 at ({pos_after[0]:.3f}, {pos_after[1]:.3f}, {pos_after[2]:.3f})")

        # Body should have moved significantly toward target (5, 5, 0)
        # At minimum, X should have increased from ~0 toward 5
        dx = pos_after[0] - pos_before[0]
        assert dx > 1.0, (
            f"Box3 X should move toward target 5.0 (dx={dx:.3f}). "
            f"Before={pos_before[0]:.3f}, After={pos_after[0]:.3f}")

        # Distance to target should be less than distance before
        dist_before = ((pos_before[0] - target[0])**2 +
                       (pos_before[1] - target[1])**2 +
                       (pos_before[2] - target[2])**2) ** 0.5
        dist_after = ((pos_after[0] - target[0])**2 +
                      (pos_after[1] - target[1])**2 +
                      (pos_after[2] - target[2])**2) ** 0.5
        print(f"  [distance] Before={dist_before:.3f}, After={dist_after:.3f}")
        assert dist_after < dist_before, (
            f"Body should be closer to target after grab. "
            f"Before={dist_before:.3f}, After={dist_after:.3f}")

        # Release grab
        provider.end_grab()

    finally:
        provider.release()


# ---------------------------------------------------------------------------
# Test 4: Release/Cleanup (no leaked shm segments)
# ---------------------------------------------------------------------------

def test_release_cleanup():
    """After release(), shared memory segments are cleaned up."""
    provider, body_map, stage = _create_provider()

    # Verify shm exists while running
    assert _shm_exists("ovphysx_ctrl"), "Control shm should exist during run"
    assert _shm_exists("ovphysx_poses"), "Pose shm should exist during run"

    # Step a few times to make sure everything is active
    for i in range(5):
        provider.step(STEP_DT, i * STEP_DT)
        time.sleep(0.005)
    time.sleep(0.05)

    # Release the provider
    t0 = time.perf_counter()
    provider.release()
    release_time = time.perf_counter() - t0
    print(f"\n  [timing] Release: {release_time:.3f}s")

    # Give OS a moment to clean up
    time.sleep(0.2)

    # Shared memory segments should be gone (worker unlinks on exit)
    ctrl_leaked = _shm_exists("ovphysx_ctrl")
    pose_leaked = _shm_exists("ovphysx_poses")

    if ctrl_leaked or pose_leaked:
        # Clean up for other tests
        from multiprocessing import shared_memory
        for name in ("ovphysx_ctrl", "ovphysx_poses"):
            try:
                shm = shared_memory.SharedMemory(name=name, create=False)
                shm.close()
                shm.unlink()
            except FileNotFoundError:
                pass

    assert not ctrl_leaked, "ovphysx_ctrl shm segment leaked after release()"
    assert not pose_leaked, "ovphysx_poses shm segment leaked after release()"
    print("  [result] No leaked shm segments")


# ---------------------------------------------------------------------------
# Main (standalone execution)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    tests = [
        ("test_worker_init", test_worker_init),
        ("test_step_and_gravity", test_step_and_gravity),
        ("test_grab_moves_body", test_grab_moves_body),
        ("test_release_cleanup", test_release_cleanup),
    ]

    passed = 0
    failed = 0
    errors = []

    print("=" * 60)
    print("PhysX Provider Test Suite")
    print("=" * 60)

    total_t0 = time.perf_counter()

    for name, fn in tests:
        print(f"\n{'─' * 60}")
        print(f"Running: {name}")
        print(f"{'─' * 60}")
        t0 = time.perf_counter()
        try:
            fn()
            elapsed = time.perf_counter() - t0
            print(f"  ✅ PASSED ({elapsed:.3f}s)")
            passed += 1
        except Exception as e:
            elapsed = time.perf_counter() - t0
            print(f"  ❌ FAILED ({elapsed:.3f}s): {e}")
            import traceback
            traceback.print_exc()
            failed += 1
            errors.append((name, str(e)))

    total_time = time.perf_counter() - total_t0

    print(f"\n{'=' * 60}")
    print(f"Results: {passed} passed, {failed} failed (total: {total_time:.3f}s)")
    print(f"{'=' * 60}")

    if errors:
        print("\nFailures:")
        for name, err in errors:
            print(f"  • {name}: {err}")
        sys.exit(1)
    else:
        print("\nAll tests passed! ✅")
        sys.exit(0)

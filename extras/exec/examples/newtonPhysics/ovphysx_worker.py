"""
ovphysx subprocess worker — runs PhysX in isolation to avoid USD dual-runtime.

Communication: shared memory for poses + control flags (hot loop),
stdout JSON line for init handshake only.

Shared memory layout:
  "ovphysx_ctrl" (28 bytes):
    byte 0:  kick flag  (parent→worker: request step)
    byte 1:  done flag  (worker→parent: poses written)
    byte 2:  quit flag  (parent→worker: exit)
    byte 3:  (padding)
    bytes 4-7:   grab_body_idx (int32, -1 = no grab)
    bytes 8-11:  grab_target_x (float32)
    bytes 12-15: grab_target_y (float32)
    bytes 16-19: grab_target_z (float32)
    bytes 20-23: dt (float32)
    bytes 24-27: sim_time (float32)

  "ovphysx_poses" (N * 7 * 4 bytes):
    N bodies × 7 float32 (px, py, pz, qx, qy, qz, qw)
"""
import sys
import os
import json
import struct
import time
import numpy as np
from multiprocessing import shared_memory

# ovphysx needs clean PYTHONPATH — no from-source USD
# This script is launched with PYTHONPATH="" by the parent.

# Control struct offsets
OFF_KICK = 0
OFF_DONE = 1
OFF_QUIT = 2
# byte 3 = padding
OFF_GRAB_IDX = 4
OFF_GRAB_X = 8
OFF_GRAB_Y = 12
OFF_GRAB_Z = 16
OFF_DT = 20
OFF_SIM_TIME = 24
CTRL_SIZE = 28


def main():
    import io
    # ---- Redirect C-level stdout/stderr to /dev/null BEFORE importing ovphysx ----
    # Keep Python-level stdout for init handshake (JSON line protocol).
    original_stdout_fd = os.dup(1)
    original_stdout = io.TextIOWrapper(
        io.FileIO(original_stdout_fd, 'w'), line_buffering=True)

    devnull_fd = os.open(os.devnull, os.O_WRONLY)
    os.dup2(devnull_fd, 1)  # C stdout → /dev/null
    os.dup2(devnull_fd, 2)  # C stderr → /dev/null
    os.close(devnull_fd)

    import ovphysx
    ovphysx.bootstrap()
    from ovphysx import PhysX, TensorType

    # ---- Parse command-line args ----
    if len(sys.argv) < 2:
        original_stdout.write(json.dumps({"error": "usage: worker.py <scene_path> [device]"}) + "\n")
        original_stdout.flush()
        sys.exit(1)

    scene_path = sys.argv[1]
    device = sys.argv[2] if len(sys.argv) > 2 else "gpu"

    # ---- Initialize PhysX ----
    try:
        physx = PhysX(device=device)
        physx.add_usd(scene_path)
        pose_binding = physx.create_tensor_binding(
            pattern='/World/*',
            tensor_type=TensorType.RIGID_BODY_POSE
        )
        prim_paths = list(pose_binding.prim_paths)
        n_bodies = len(prim_paths)
        poses_buf = np.zeros(pose_binding.shape, dtype=np.float32)

        # Velocity binding for grab
        vel_binding = physx.create_tensor_binding(
            pattern='/World/*',
            tensor_type=TensorType.RIGID_BODY_VELOCITY
        )
        vel_buf = np.zeros(vel_binding.shape, dtype=np.float32)

    except Exception as e:
        original_stdout.write(json.dumps({"error": str(e)}) + "\n")
        original_stdout.flush()
        sys.exit(1)

    # ---- Create shared memory segments ----
    pose_shm_size = n_bodies * 7 * 4  # float32

    # Clean up any stale shm from previous run
    for name in ("ovphysx_ctrl", "ovphysx_poses"):
        try:
            stale = shared_memory.SharedMemory(name=name, create=False)
            stale.close()
            stale.unlink()
        except FileNotFoundError:
            pass

    ctrl_shm = shared_memory.SharedMemory(name="ovphysx_ctrl", create=True, size=CTRL_SIZE)
    pose_shm = shared_memory.SharedMemory(name="ovphysx_poses", create=True, size=pose_shm_size)

    # Zero out control block
    ctrl_shm.buf[:CTRL_SIZE] = b'\x00' * CTRL_SIZE
    # Set grab_body_idx = -1 (no grab)
    struct.pack_into('<i', ctrl_shm.buf, OFF_GRAB_IDX, -1)

    # Create numpy view over pose shm for fast writes
    pose_view = np.ndarray((n_bodies, 7), dtype=np.float32, buffer=pose_shm.buf)

    # ---- Send init response (synchronous handshake over stdout) ----
    original_stdout.write(json.dumps({
        "status": "ok",
        "prim_paths": prim_paths,
        "n_bodies": n_bodies,
        "ctrl_shm": "ovphysx_ctrl",
        "pose_shm": "ovphysx_poses",
        "pose_shm_size": pose_shm_size,
    }) + "\n")
    original_stdout.flush()

    # Close stdout — no more pipe communication needed
    original_stdout.close()

    # ---- Grab state ----
    grab_stiffness = 20.0
    num_substeps = 2

    # ---- Hot loop: watch kick flag, step physics, write poses ----
    try:
        while True:
            # Check quit flag
            if ctrl_shm.buf[OFF_QUIT]:
                break

            # Check kick flag (spin-wait with brief sleep to avoid 100% CPU)
            if not ctrl_shm.buf[OFF_KICK]:
                time.sleep(0.0001)  # 100μs — sub-frame granularity
                continue

            # Read dt and sim_time from control block
            dt = struct.unpack_from('<f', ctrl_shm.buf, OFF_DT)[0]
            sim_time = struct.unpack_from('<f', ctrl_shm.buf, OFF_SIM_TIME)[0]

            # Clear kick flag (acknowledge)
            ctrl_shm.buf[OFF_KICK] = 0

            # Read grab state from control block
            grab_body_idx = struct.unpack_from('<i', ctrl_shm.buf, OFF_GRAB_IDX)[0]

            sub_dt = dt / num_substeps

            # Apply grab velocity if active
            if grab_body_idx >= 0:
                grab_x = struct.unpack_from('<f', ctrl_shm.buf, OFF_GRAB_X)[0]
                grab_y = struct.unpack_from('<f', ctrl_shm.buf, OFF_GRAB_Y)[0]
                grab_z = struct.unpack_from('<f', ctrl_shm.buf, OFF_GRAB_Z)[0]

                pose_binding.read(poses_buf)
                body_pos = poses_buf[grab_body_idx, :3].astype(np.float64)
                target = np.array([grab_x, grab_y, grab_z], dtype=np.float64)
                delta = target - body_pos
                vel = delta * grab_stiffness
                speed = np.linalg.norm(vel)
                if speed > 20.0:
                    vel *= 20.0 / speed

                vel_buf[:] = 0
                vel_buf[grab_body_idx, 0] = float(vel[0])
                vel_buf[grab_body_idx, 1] = float(vel[1])
                vel_buf[grab_body_idx, 2] = float(vel[2])
                vel_binding.write(vel_buf)

            # Fire all substeps (async / stream-ordered)
            for s in range(num_substeps):
                physx.step(sub_dt, sim_time + s * sub_dt)

            # Read poses and write to shared memory
            pose_binding.read(poses_buf)
            np.copyto(pose_view, poses_buf)

            # Signal done
            ctrl_shm.buf[OFF_DONE] = 1

    except KeyboardInterrupt:
        pass
    finally:
        # Cleanup
        try:
            physx.release()
        except Exception:
            pass
        ctrl_shm.close()
        ctrl_shm.unlink()
        pose_shm.close()
        pose_shm.unlink()

    sys.exit(0)


if __name__ == "__main__":
    main()

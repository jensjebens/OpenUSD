"""
ovphysx subprocess worker — runs PhysX in isolation to avoid USD dual-runtime.

Communication via stdin/stdout (JSON lines) + shared memory for poses.
The parent usdview process sends commands, this process steps PhysX and
writes pose data to a memory-mapped file for zero-copy reads.
"""
import sys
import os
import json
import mmap
import struct
import numpy as np

# ovphysx needs clean PYTHONPATH — no from-source USD
# This script is launched with PYTHONPATH="" by the parent.


def main():
    # Redirect C-level stdout/stderr to /dev/null BEFORE importing ovphysx,
    # so carb/PhysX warnings don't corrupt our JSON line protocol on fd 1.
    # We keep Python's sys.stdout/stderr pointing to the real fds.
    import ctypes
    import io

    # Save original stdout fd for our JSON protocol
    original_stdout_fd = os.dup(1)
    original_stdout = io.TextIOWrapper(
        io.FileIO(original_stdout_fd, 'w'), line_buffering=True)

    # Redirect C-level fd 1 and 2 to /dev/null
    devnull_fd = os.open(os.devnull, os.O_WRONLY)
    os.dup2(devnull_fd, 1)  # C stdout → /dev/null
    os.dup2(devnull_fd, 2)  # C stderr → /dev/null
    os.close(devnull_fd)

    import ovphysx
    ovphysx.bootstrap()
    from ovphysx import PhysX, TensorType

    physx = None
    pose_binding = None
    vel_bindings = {}
    poses_buf = None
    prim_paths = []
    mmap_file = None
    mmap_path = "/tmp/ovphysx_poses.mmap"

    # Grab state
    grab_body_idx = -1
    grab_target = None
    grab_stiffness = 20.0

    # Articulation wrench binding (for jointed bodies)
    wrench_binding = None
    wrench_buf = None
    is_articulated = False

    # Substeps for solver stability (configured on init)
    num_substeps = 2

    def respond(msg):
        original_stdout.write(json.dumps(msg) + "\n")
        original_stdout.flush()

    respond({"status": "ready"})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            cmd = json.loads(line)
        except json.JSONDecodeError:
            respond({"error": "invalid json"})
            continue

        action = cmd.get("action")

        if action == "init":
            scene_path = cmd["scene_path"]
            device = cmd.get("device", "gpu")
            try:
                physx = PhysX(device=device)
                physx.add_usd(scene_path)
                pose_binding = physx.create_tensor_binding(
                    pattern='/World/*',
                    tensor_type=TensorType.RIGID_BODY_POSE
                )
                prim_paths = list(pose_binding.prim_paths)
                poses_buf = np.zeros(pose_binding.shape, dtype=np.float32)

                # Try articulation wrench for jointed bodies (preferred for grab)
                try:
                    wrench_binding = physx.create_tensor_binding(
                        pattern='/World/*',
                        tensor_type=TensorType.ARTICULATION_LINK_WRENCH
                    )
                    wrench_buf = np.zeros(wrench_binding.shape, dtype=np.float32)
                    is_articulated = True
                except Exception:
                    is_articulated = False

                # Single velocity binding for all bodies (works for free rigid bodies)
                vel_binding = physx.create_tensor_binding(
                    pattern='/World/*',
                    tensor_type=TensorType.RIGID_BODY_VELOCITY
                )
                vel_buf = np.zeros(vel_binding.shape, dtype=np.float32)

                # Create memory-mapped file for poses
                # Layout: N bodies x 7 floats (px,py,pz,qx,qy,qz,qw)
                n_bodies = len(prim_paths)
                mmap_size = n_bodies * 7 * 4  # float32
                with open(mmap_path, "wb") as f:
                    f.write(b'\x00' * mmap_size)
                mmap_file = open(mmap_path, "r+b")

                respond({
                    "status": "ok",
                    "prim_paths": prim_paths,
                    "n_bodies": n_bodies,
                    "mmap_path": mmap_path,
                    "mmap_size": mmap_size,
                })
            except Exception as e:
                respond({"error": str(e)})

        elif action == "step":
            dt = cmd.get("dt", 1.0/60.0)
            sim_time = cmd.get("sim_time", 0.0)
            try:
                sub_dt = dt / num_substeps

                # Apply grab velocity once (read current pose, compute vel)
                if grab_body_idx >= 0 and grab_target is not None:
                    pose_binding.read(poses_buf)  # blocking read
                    body_pos = poses_buf[grab_body_idx, :3].astype(np.float64)
                    target = np.array(grab_target, dtype=np.float64)
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

                # Fire all substeps without blocking between them
                # (PhysX step() is async / stream-ordered)
                for s in range(num_substeps):
                    physx.step(sub_dt, sim_time + s * sub_dt)

                # Read poses and write to mmap
                pose_binding.read(poses_buf)
                mmap_file.seek(0)
                mmap_file.write(poses_buf.tobytes())
                mmap_file.flush()

                respond({"status": "ok"})
            except Exception as e:
                respond({"error": str(e)})

        elif action == "grab":
            grab_body_idx = cmd.get("body_idx", -1)
            grab_target = cmd.get("target")
            respond({"status": "ok"})

        elif action == "update_grab":
            grab_target = cmd.get("target")
            respond({"status": "ok"})

        elif action == "end_grab":
            grab_body_idx = -1
            grab_target = None
            respond({"status": "ok"})

        elif action == "release":
            if physx:
                physx.release()
                physx = None
            if mmap_file:
                mmap_file.close()
                mmap_file = None
            respond({"status": "ok"})
            break

        elif action == "ping":
            respond({"status": "pong"})

        else:
            respond({"error": f"unknown action: {action}"})

    sys.exit(0)


if __name__ == "__main__":
    main()

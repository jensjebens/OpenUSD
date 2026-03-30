#!/usr/bin/env python3
"""
newtonRecord — render USD physics with Newton GPU through the live
HdExec TransformProvider pipeline (no baking).

Registers a Newton GPU TransformProvider, then invokes usdrecord.
The provider is called by HdExec's _ExecMatrixDataSource on each
frame for each physics prim.

Usage:
    python newtonRecord.py [usdrecord args...] input.usda output.####.png
"""

import os
import sys
import subprocess

# Ensure USD Python is on path.
USD_BUILD = os.environ.get("USD_BUILD",
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "../../../../usd_build"))
sys.path.insert(0, os.path.join(USD_BUILD, "lib", "python"))

from pxr import Usd, Sdf, Gf, HdExec

import newton
import warp as wp


def setup_provider(usda_path, solver_name="xpbd", device="cuda:0"):
    """Initialize Newton GPU and register the TransformProvider."""

    stage = Usd.Stage.Open(usda_path)
    if not stage:
        print(f"[Newton GPU] ERROR: cannot open {usda_path}")
        return None

    builder = newton.ModelBuilder()
    builder.add_usd(stage)
    model = builder.finalize(device=device)

    body_map = {}
    for idx, label in enumerate(builder.body_label):
        body_map[Sdf.Path(label)] = idx

    solver_map = {
        "xpbd": newton.solvers.SolverXPBD,
        "mujoco": newton.solvers.SolverMuJoCo,
        "featherstone": newton.solvers.SolverFeatherstone,
        "semi_implicit": newton.solvers.SolverSemiImplicit,
    }
    solver = solver_map.get(solver_name, newton.solvers.SolverXPBD)(model)

    # Use two state objects and a list to track which is current.
    states = [model.state(), model.state()]
    sim_state = {"current": 0, "time": 0.0}

    fps = stage.GetTimeCodesPerSecond()
    if fps <= 0:
        fps = 60.0
    dt = 1.0 / fps

    def provider(prim_path, time_seconds):
        s = sim_state
        cur = s["current"]

        while s["time"] < time_seconds - dt * 0.5:
            nxt = 1 - cur
            contacts = model.collide(states[cur])
            solver.step(states[cur], states[nxt],
                        control=None, contacts=contacts, dt=dt)
            cur = nxt
            s["current"] = cur
            s["time"] += dt

        idx = body_map.get(prim_path)
        if idx is None:
            return None

        tf = states[cur].body_q.numpy()[idx]
        px, py, pz = float(tf[0]), float(tf[1]), float(tf[2])
        qx, qy, qz, qw = float(tf[3]), float(tf[4]), float(tf[5]), float(tf[6])

        quat = Gf.Quatd(qw, qx, qy, qz)
        mat = Gf.Matrix4d()
        mat.SetRotate(quat)
        mat.SetTranslateOnly(Gf.Vec3d(px, py, pz))
        return mat

    HdExec.RegisterTransformProvider("newtonGPU", provider)
    HdExec.SetGlobalStage(stage)

    print(f"[Newton GPU] {len(body_map)} bodies, solver={solver_name}, "
          f"device={device}, fps={fps}")
    return stage


def main():
    # Find the USD file in argv (last .usda/.usd/.usdc arg before output).
    usda_path = None
    for arg in sys.argv[1:]:
        if arg.endswith((".usda", ".usd", ".usdc")):
            usda_path = arg
            break

    if not usda_path:
        print("Usage: newtonRecord.py [usdrecord args] input.usda output.####.png")
        return 1

    # Set up Newton provider.
    setup_provider(usda_path)

    # Run usdrecord in-process so the registered provider persists.
    usdrecord_path = os.path.join(USD_BUILD, "bin", "usdrecord")

    # Patch sys.argv for usdrecord's argparse.
    orig_argv = sys.argv
    sys.argv = ["usdrecord"] + orig_argv[1:]

    try:
        with open(usdrecord_path) as f:
            code = f.read()
        # Remove the if __name__ == "__main__" guard and sys.exit wrapper
        # so we can exec it in our process.
        exec(compile(code, usdrecord_path, "exec"), {"__name__": "__main__"})
    except SystemExit as e:
        return e.code or 0
    finally:
        sys.argv = orig_argv

    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)

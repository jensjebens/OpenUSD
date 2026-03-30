# SPDX-License-Identifier: Apache-2.0

"""Newton GPU physics engine wrapper.

Manages the Newton Model, Solver, and State lifecycle. Parses a USD stage
via ModelBuilder.add_usd(), steps the simulation, and provides body
transforms as GfMatrix4d.
"""

import math
from typing import Optional

try:
    import newton
    import warp as wp
    HAS_NEWTON = True
except ImportError:
    HAS_NEWTON = False

from pxr import Gf, Sdf, UsdPhysics


# Map solver name → class
_SOLVER_MAP = {}
if HAS_NEWTON:
    _SOLVER_MAP = {
        "xpbd": newton.solvers.SolverXPBD,
        "mujoco": newton.solvers.SolverMuJoCo,
        "featherstone": newton.solvers.SolverFeatherstone,
        "semi_implicit": newton.solvers.SolverSemiImplicit,
        "vbd": newton.solvers.SolverVBD,
        "implicit_mpm": newton.solvers.SolverImplicitMPM,
        "kamino": newton.solvers.SolverKamino,
        "style3d": newton.solvers.SolverStyle3D,
    }


class NewtonEngine:
    """Wraps Newton GPU's Model + Solver + State for USD physics simulation.

    Usage:
        engine = NewtonEngine()
        engine.initialize(stage, solver_name="xpbd")
        engine.step(dt=1/60.0)
        xform = engine.get_transform(Sdf.Path("/World/Box"))
    """

    def __init__(self, device: str = "cuda:0"):
        self._device = device
        self._model = None
        self._solver = None
        self._state_0 = None
        self._state_1 = None
        self._contacts = None
        self._builder = None
        self._stage = None
        self._initialized = False
        self._current_time = 0.0
        self._body_path_to_index = {}
        self._solver_name = "xpbd"
        self._picking = None

    @property
    def initialized(self) -> bool:
        return self._initialized

    @property
    def model(self):
        return self._model

    @property
    def solver_name(self) -> str:
        return self._solver_name

    def initialize(self, stage, solver_name: str = "xpbd"):
        """Initialize the physics engine from a USD stage.

        Args:
            stage: A UsdStage with UsdPhysics schemas.
            solver_name: One of "xpbd", "mujoco", "featherstone",
                "semi_implicit", "vbd", "implicit_mpm", "kamino", "style3d".
        """
        if not HAS_NEWTON:
            raise RuntimeError("Newton GPU not installed. pip install newton")

        self._stage = stage
        self._solver_name = solver_name

        # Read solver config from NewtonSceneAPI / solver-specific APIs
        # on the PhysicsScene prim (newton-usd-schemas).
        self._time_steps_per_second = 1000
        for prim in stage.Traverse():
            if prim.IsA(UsdPhysics.Scene):
                # newton:timeStepsPerSecond from NewtonSceneAPI
                ts_attr = prim.GetAttribute("newton:timeStepsPerSecond")
                if ts_attr and ts_attr.HasAuthoredValue():
                    self._time_steps_per_second = int(ts_attr.Get())

                # Detect solver from applied API schemas.
                # NewtonXpbdSceneAPI → xpbd, NewtonKaminoSceneAPI → kamino
                schemas = prim.GetAppliedSchemas()
                for schema in schemas:
                    s = str(schema)
                    if "XpbdScene" in s:
                        self._solver_name = "xpbd"
                    elif "KaminoScene" in s:
                        self._solver_name = "kamino"

                # Explicit override via custom attribute (backward compat).
                solver_attr = prim.GetAttribute("newton:solver")
                if solver_attr and solver_attr.HasAuthoredValue():
                    val = str(solver_attr.Get())
                    if val in _SOLVER_MAP:
                        self._solver_name = val
                break

        # Build the model from USD.
        self._builder = newton.ModelBuilder(device=self._device)
        parse_result = self._builder.add_usd(stage)

        # Finalize the model.
        self._model = self._builder.finalize(device=self._device)

        # Build the body path → index mapping from body labels.
        # Newton's USD parser stores prim paths as body labels.
        self._body_path_to_index = {}
        labels = (self._model.body_label
                  if hasattr(self._model, 'body_label')
                  else self._builder.body_label)
        for idx, label in enumerate(labels):
            self._body_path_to_index[Sdf.Path(label)] = idx

        # Create the solver.
        solver_cls = _SOLVER_MAP.get(self._solver_name)
        if solver_cls is None:
            raise ValueError(
                f"Unknown solver '{self._solver_name}'. "
                f"Available: {list(_SOLVER_MAP.keys())}")
        self._solver = solver_cls(self._model)

        # Allocate states.
        self._state_0 = self._model.state()
        self._state_1 = self._model.state()

        # Allocate contacts if the model has collision shapes.
        if self._model.shape_count > 0:
            self._contacts = self._model.collide(self._state_0)

        # Set up interactive picking.
        self._picking = newton._src.viewer.picking.Picking(self._model)

        self._current_time = 0.0
        self._initialized = True

    def step(self, dt: float = None):
        """Advance the simulation by dt seconds.
        
        If dt is None, uses 1/timeStepsPerSecond from NewtonSceneAPI.
        """
        if not self._initialized:
            return
        if dt is None:
            dt = 1.0 / self._time_steps_per_second

        # Collision detection.
        if self._contacts is not None:
            self._contacts = self._model.collide(self._state_0)

        # Apply interactive picking forces if active.
        if self._picking and self._picking.is_picking():
            self._picking._apply_picking_force(self._state_0)

        # Step the solver.
        self._solver.step(
            self._state_0, self._state_1,
            control=None,
            contacts=self._contacts,
            dt=dt)

        # Swap states.
        self._state_0, self._state_1 = self._state_1, self._state_0
        self._current_time += dt

    def advance_to_time(self, time_seconds: float):
        """Step to reach the given simulation time."""
        if not self._initialized:
            return
        dt = 1.0 / self._time_steps_per_second
        while self._current_time < time_seconds - dt * 0.5:
            self.step(dt)

    def get_transform(self, prim_path: Sdf.Path) -> Optional[Gf.Matrix4d]:
        """Get the simulated world-space transform for a body.

        Returns None if the prim is not a simulated body.
        """
        if not self._initialized:
            return None

        idx = self._body_path_to_index.get(prim_path)
        if idx is None:
            return None

        # Read body_q (7-DOF: pos.x, pos.y, pos.z, quat.x, quat.y, quat.z, quat.w)
        body_q = self._state_0.body_q.numpy()
        if idx >= len(body_q):
            return None

        tf = body_q[idx]
        # Newton uses (px, py, pz, qx, qy, qz, qw) layout
        px, py, pz = float(tf[0]), float(tf[1]), float(tf[2])
        qx, qy, qz, qw = float(tf[3]), float(tf[4]), float(tf[5]), float(tf[6])

        # Build GfMatrix4d from quaternion + translation.
        quat = Gf.Quatd(qw, qx, qy, qz)
        rotation = Gf.Matrix4d()
        rotation.SetRotate(quat)
        translation = Gf.Matrix4d()
        translation.SetTranslate(Gf.Vec3d(px, py, pz))

        return rotation * translation

    def has_body(self, prim_path: Sdf.Path) -> bool:
        """Returns True if the prim is a simulated body."""
        return prim_path in self._body_path_to_index

    def get_body_count(self) -> int:
        """Returns the number of simulated bodies."""
        return len(self._body_path_to_index)

    def get_body_paths(self) -> list:
        """Returns all simulated body prim paths."""
        return list(self._body_path_to_index.keys())

    # --- Interactive picking ---

    def begin_pick(self, ray_start, ray_dir):
        """Begin interactive body picking (right-click-drag)."""
        if not self._initialized or not self._picking:
            return False
        self._picking.pick(
            self._state_0,
            wp.vec3f(float(ray_start[0]), float(ray_start[1]), float(ray_start[2])),
            wp.vec3f(float(ray_dir[0]), float(ray_dir[1]), float(ray_dir[2])))
        return self._picking.is_picking()

    def update_pick(self, ray_start, ray_dir):
        """Update pick target during drag."""
        if not self._picking or not self._picking.is_picking():
            return
        self._picking.update(
            wp.vec3f(float(ray_start[0]), float(ray_start[1]), float(ray_start[2])),
            wp.vec3f(float(ray_dir[0]), float(ray_dir[1]), float(ray_dir[2])))

    def release_pick(self):
        """Release the picked body."""
        if self._picking:
            self._picking.release()

    def reset(self):
        """Reset the simulation."""
        self._model = None
        self._solver = None
        self._state_0 = None
        self._state_1 = None
        self._contacts = None
        self._picking = None
        self._initialized = False
        self._current_time = 0.0
        self._body_path_to_index = {}

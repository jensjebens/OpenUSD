# Newton/PhysX interactive physics plugin for UsdView.
#
# Physics menu with engine selector:
#   - Start/Stop Physics (runs whichever engine is selected)
#   - Enable/Disable Grab Mode (shift+left-click-drag)
#   - Reset Simulation
#   - Engine: Newton GPU / PhysX 5 (toggle between providers)
#
# Installation:
#   export PXR_PLUGINPATH_NAME=/path/to/usdviewPlugin:$PXR_PLUGINPATH_NAME
#   export PYTHONPATH=/path/to/newtonPhysics:$PYTHONPATH
#   usdview scene.usda

import os
import sys
import json
import math
import logging
import traceback

import numpy as np

# File logger for debugging — usdview swallows stdout/stderr.
_LOG_PATH = "/tmp/newton_plugin.log"
logging.basicConfig(
    filename=_LOG_PATH,
    level=logging.DEBUG,
    format="%(asctime)s [Newton] %(message)s",
    datefmt="%H:%M:%S",
)
_log = logging.getLogger("newton_usdview")
_log.info("Plugin module loaded")

from pxr import Tf, Sdf, Gf, Usd, UsdGeom, UsdPhysics, Work

try:
    from pxr import HdExec
    HAS_HDEXEC = True
except ImportError:
    HAS_HDEXEC = False

try:
    import newton
    import warp as wp
    HAS_NEWTON = True
except ImportError:
    HAS_NEWTON = False

# ovphysx can't coexist in-process with from-source USD (dual runtime).
# We check if the package exists but run it in a subprocess.
import importlib.util
HAS_PHYSX = importlib.util.find_spec("ovphysx") is not None

from pxr.Usdviewq.plugin import PluginContainer


# ======================================================================
# Provider Interface
# ======================================================================

class PhysicsProvider:
    """Abstract base for physics providers."""

    @property
    def name(self) -> str:
        raise NotImplementedError

    @property
    def needs_axis_swap(self) -> bool:
        """Whether this provider simulates in Z-up and needs Y↔Z conversion."""
        return False

    def initialize(self, scene_path: str, stage, device: str = 'gpu') -> dict:
        """Initialize and return {sdf_path: body_index} mapping."""
        raise NotImplementedError

    def step(self, dt: float, sim_time: float):
        raise NotImplementedError

    def get_body_transforms(self, swap_yz: bool) -> list:
        """Return list of (Sdf.Path, Gf.Matrix4d) for HdExec."""
        raise NotImplementedError

    def begin_grab(self, body_idx: int, target_pos):
        """Start grabbing body at index."""
        pass

    def update_grab(self, target_pos):
        """Update grab target position (in Newton/sim space)."""
        pass

    def end_grab(self):
        """Release grabbed body."""
        pass

    def release(self):
        """Clean up resources."""
        pass


# ======================================================================
# Newton GPU Provider
# ======================================================================

class NewtonProvider(PhysicsProvider):
    """Newton GPU via warp/newton pip package."""

    @property
    def name(self):
        return "Newton GPU"

    @property
    def needs_axis_swap(self):
        """Newton always simulates in Z-up."""
        return True

    def initialize(self, scene_path, stage, device='gpu'):
        newton_device = 'cuda:0' if wp.is_cuda_available() else 'cpu'
        _log.info(f"[Newton] Initializing on {newton_device}...")

        # Open separate stage to avoid Hydra contention
        newton_stage = Usd.Stage.Open(scene_path)

        builder = newton.ModelBuilder()
        Work.SetConcurrencyLimit(1)
        try:
            builder.add_usd(newton_stage, skip_mesh_approximation=True)
        finally:
            Work.SetConcurrencyLimit(0)
        builder.approximate_meshes(method='convex_hull')
        self._model = builder.finalize(device=newton_device)

        self._body_map = {}
        for idx, label in enumerate(builder.body_label):
            self._body_map[Sdf.Path(label)] = idx

        self._solver = newton.solvers.SolverXPBD(self._model)
        self._states = [self._model.state(), self._model.state()]
        self._current = 0
        self._grab_body_idx = -1
        self._grab_target = None

        _log.info(f"[Newton] {len(self._body_map)} bodies ready")
        return self._body_map

    def step(self, dt, sim_time):
        cur = self._current
        nxt = 1 - cur

        # Apply grab velocity before stepping
        if self._grab_body_idx >= 0 and self._grab_target is not None:
            body_qd = self._states[cur].body_qd.numpy()
            body_q = self._states[cur].body_q.numpy()
            tf = body_q[self._grab_body_idx]
            pos = np.array([tf[0], tf[1], tf[2]], dtype=np.float64)
            target = np.array(self._grab_target, dtype=np.float64)
            delta = target - pos
            vel = delta * 5.0  # gain
            speed = np.linalg.norm(vel)
            if speed > 20.0:
                vel *= 20.0 / speed
            body_qd[self._grab_body_idx][0] = vel[0]
            body_qd[self._grab_body_idx][1] = vel[1]
            body_qd[self._grab_body_idx][2] = vel[2]
            body_qd[self._grab_body_idx][3:6] = 0.0
            self._states[cur].body_qd.assign(
                wp.array(body_qd, dtype=wp.spatial_vectorf,
                         device=self._model.device))

        contacts = self._model.collide(self._states[cur])
        self._solver.step(
            self._states[cur], self._states[nxt],
            control=None, contacts=contacts, dt=dt)
        self._current = nxt

    def get_body_transforms(self, swap_yz):
        body_q = self._states[self._current].body_q.numpy()

        if swap_yz:
            q_undo = Gf.Quatd(math.cos(-math.pi/4), math.sin(-math.pi/4), 0, 0)

        transforms = []
        for path, idx in self._body_map.items():
            tf = body_q[idx]
            qx, qy, qz, qw = float(tf[3]), float(tf[4]), float(tf[5]), float(tf[6])
            q_newton = Gf.Quatd(qw, qx, qy, qz)
            pos_newton = Gf.Vec3d(float(tf[0]), float(tf[1]), float(tf[2]))

            if swap_yz:
                q_usd = q_undo * q_newton
                pos_usd = q_undo.Transform(pos_newton)
            else:
                q_usd = q_newton
                pos_usd = pos_newton

            M = Gf.Matrix4d()
            M.SetRotate(q_usd)
            M.SetTranslateOnly(pos_usd)
            transforms.append((path, M))

        return transforms

    def begin_grab(self, body_idx, target_pos):
        self._grab_body_idx = body_idx
        self._grab_target = target_pos
        _log.info(f"[Newton] Grab body {body_idx}")

    def update_grab(self, target_pos):
        self._grab_target = target_pos

    def end_grab(self):
        self._grab_body_idx = -1
        self._grab_target = None

    def release(self):
        self._model = None
        self._solver = None
        self._states = None


# ======================================================================
# PhysX 5 Provider (ovphysx) — shared-memory decoupled architecture
# ======================================================================

class PhysXProvider(PhysicsProvider):
    """NVIDIA PhysX 5 via ovphysx — runs in a subprocess communicating
    through shared memory for zero-copy, non-blocking pose transfer.

    Architecture:
      - Init: synchronous pipe handshake (stdout JSON line)
      - Hot loop: parent sets kick flag in shm, worker steps & writes poses,
        sets done flag. Parent reads poses non-blocking (1-frame latency).
      - Grab: parent writes body_idx + target to shm control segment.
      - Shutdown: parent sets quit flag.
    """

    # Control struct offsets (must match worker)
    _OFF_KICK = 0
    _OFF_DONE = 1
    _OFF_QUIT = 2
    _OFF_GRAB_IDX = 4
    _OFF_GRAB_X = 8
    _OFF_GRAB_Y = 12
    _OFF_GRAB_Z = 16
    _OFF_DT = 20
    _OFF_SIM_TIME = 24
    _CTRL_SIZE = 28

    @property
    def name(self):
        return "PhysX 5"

    def initialize(self, scene_path, stage, device='gpu'):
        import subprocess
        import struct as _struct
        from multiprocessing import shared_memory

        self._struct = _struct
        self._shm_module = shared_memory

        _log.info("[PhysX] Launching subprocess worker (shm mode)...")

        # Find the worker script (sibling of the usdviewPlugin package)
        worker_path = os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "ovphysx_worker.py")

        # Launch with clean PYTHONPATH so ovphysx uses its bundled USD
        env = os.environ.copy()
        env["PYTHONPATH"] = ""
        env.pop("LD_PRELOAD", None)

        python = sys.executable  # same venv python (has ovphysx)
        self._proc = subprocess.Popen(
            [python, worker_path, scene_path, device],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            text=True,
            bufsize=1,
        )

        # Synchronous init handshake — read single JSON line from stdout
        line = self._proc.stdout.readline()
        if not line:
            stderr = self._proc.stderr.read() if self._proc.stderr else ""
            raise RuntimeError(f"Worker died during init: {stderr[:500]}")
        resp = json.loads(line)
        if "error" in resp:
            raise RuntimeError(f"PhysX init failed: {resp['error']}")

        prim_paths = resp["prim_paths"]
        self._n_bodies = resp["n_bodies"]
        pose_shm_size = resp["pose_shm_size"]

        # Attach to shared memory segments created by worker
        self._ctrl_shm = shared_memory.SharedMemory(
            name="ovphysx_ctrl", create=False)
        self._pose_shm = shared_memory.SharedMemory(
            name="ovphysx_poses", create=False)

        # Numpy view over pose buffer (zero-copy reads)
        self._poses_view = np.ndarray(
            (self._n_bodies, 7), dtype=np.float32,
            buffer=self._pose_shm.buf)

        # Build body map
        self._body_map = {}
        for idx, path_str in enumerate(prim_paths):
            self._body_map[Sdf.Path(path_str)] = idx

        _log.info(f"[PhysX] {len(self._body_map)} bodies ready (shm decoupled)")
        return self._body_map

    def step(self, dt, sim_time):
        """Non-blocking: write dt/sim_time to control shm and set kick flag.
        The worker will step asynchronously; poses appear in shm when done.
        """
        buf = self._ctrl_shm.buf
        # Write dt and sim_time
        self._struct.pack_into('<f', buf, self._OFF_DT, dt)
        self._struct.pack_into('<f', buf, self._OFF_SIM_TIME, sim_time)
        # Set kick flag (worker is spinning on this)
        buf[self._OFF_KICK] = 1

    def get_body_transforms(self, swap_yz):
        """Read poses directly from shared memory numpy view.
        Non-blocking — returns last available poses even if worker
        hasn't finished the current step yet (graceful 1-frame latency).
        """
        # Optionally clear done flag so we know when *next* frame lands
        buf = self._ctrl_shm.buf
        if buf[self._OFF_DONE]:
            buf[self._OFF_DONE] = 0

        # Read poses (numpy view — no copy needed for iteration)
        poses_buf = self._poses_view

        if swap_yz:
            q_undo = Gf.Quatd(math.cos(-math.pi/4), math.sin(-math.pi/4), 0, 0)

        transforms = []
        for path, idx in self._body_map.items():
            tf = poses_buf[idx]
            px, py, pz = float(tf[0]), float(tf[1]), float(tf[2])
            qx, qy, qz, qw = float(tf[3]), float(tf[4]), float(tf[5]), float(tf[6])
            q_sim = Gf.Quatd(qw, qx, qy, qz)
            pos_sim = Gf.Vec3d(px, py, pz)

            if swap_yz:
                q_usd = q_undo * q_sim
                pos_usd = q_undo.Transform(pos_sim)
            else:
                q_usd = q_sim
                pos_usd = pos_sim

            M = Gf.Matrix4d()
            M.SetRotate(q_usd)
            M.SetTranslateOnly(pos_usd)
            transforms.append((path, M))

        return transforms

    def begin_grab(self, body_idx, target_pos):
        """Write grab body index and target to shm control segment."""
        buf = self._ctrl_shm.buf
        self._struct.pack_into('<i', buf, self._OFF_GRAB_IDX, body_idx)
        self._struct.pack_into('<f', buf, self._OFF_GRAB_X, float(target_pos[0]))
        self._struct.pack_into('<f', buf, self._OFF_GRAB_Y, float(target_pos[1]))
        self._struct.pack_into('<f', buf, self._OFF_GRAB_Z, float(target_pos[2]))
        _log.info(f"[PhysX] Grab body {body_idx}")

    def update_grab(self, target_pos):
        """Update grab target position in shm — non-blocking."""
        buf = self._ctrl_shm.buf
        self._struct.pack_into('<f', buf, self._OFF_GRAB_X, float(target_pos[0]))
        self._struct.pack_into('<f', buf, self._OFF_GRAB_Y, float(target_pos[1]))
        self._struct.pack_into('<f', buf, self._OFF_GRAB_Z, float(target_pos[2]))

    def end_grab(self):
        """Clear grab by setting body_idx to -1."""
        buf = self._ctrl_shm.buf
        self._struct.pack_into('<i', buf, self._OFF_GRAB_IDX, -1)

    def release(self):
        """Signal worker to quit and clean up shared memory."""
        try:
            if hasattr(self, '_ctrl_shm') and self._ctrl_shm:
                self._ctrl_shm.buf[self._OFF_QUIT] = 1
        except Exception:
            pass

        # Wait for worker to exit gracefully
        if hasattr(self, '_proc') and self._proc:
            try:
                self._proc.wait(timeout=3)
            except Exception:
                self._proc.terminate()
                try:
                    self._proc.wait(timeout=2)
                except Exception:
                    self._proc.kill()
            self._proc = None

        # Close shm handles (worker unlinks them on exit)
        if hasattr(self, '_ctrl_shm') and self._ctrl_shm:
            try:
                self._ctrl_shm.close()
            except Exception:
                pass
            self._ctrl_shm = None
        if hasattr(self, '_pose_shm') and self._pose_shm:
            try:
                self._pose_shm.close()
            except Exception:
                pass
            self._pose_shm = None


# ======================================================================
# UsdView Plugin Container
# ======================================================================

class NewtonPhysicsPlugin(PluginContainer):
    """UsdView plugin for interactive physics with engine selector."""

    def registerPlugins(self, plugRegistry, plugCtx):
        _log.info("registerPlugins called!")
        self._ctx = plugCtx
        self._provider = None
        self._running = False
        self._event_filter = None
        self._stageView = None
        self._sim_timer = None

        # Default engine selection
        if HAS_NEWTON:
            self._selected_engine = "newton"
        elif HAS_PHYSX:
            self._selected_engine = "physx"
        else:
            self._selected_engine = None

        self._toggle_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.togglePhysics",
            "Simulate",
            lambda ctx: self._safeCall(self._togglePhysics, ctx))

        self._grab_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.toggleGrab",
            "Grab Mode",
            lambda ctx: self._safeCall(self._toggleGrab, ctx))

        self._reset_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.resetPhysics",
            "Reset",
            lambda ctx: self._safeCall(self._resetPhysics, ctx))

        # Engine commands — one per available engine
        if HAS_NEWTON:
            self._newton_cmd = plugRegistry.registerCommandPlugin(
                "NewtonPhysicsPlugin.selectNewton",
                "Newton GPU",
                lambda ctx: self._safeCall(self._selectEngine, ctx, "newton"))
        if HAS_PHYSX:
            self._physx_cmd = plugRegistry.registerCommandPlugin(
                "NewtonPhysicsPlugin.selectPhysX",
                "PhysX 5",
                lambda ctx: self._safeCall(self._selectEngine, ctx, "physx"))

    def configureView(self, plugRegistry, plugUIBuilder):
        _log.info("configureView called!")

        try:
            from PySide6.QtGui import QActionGroup, QKeySequence
            from PySide6.QtWidgets import QMenuBar, QMenu
        except ImportError:
            from PySide2.QtWidgets import QActionGroup, QMenuBar, QMenu
            from PySide2.QtGui import QKeySequence

        # Find the render menu bar (above the viewport, next to "Lights")
        # rather than the top-level window menu bar.
        mainWindow = plugUIBuilder._mainWindow
        renderMenuBar = mainWindow.findChild(QMenuBar, "renderMenuBar")

        if renderMenuBar:
            # Add our Physics menu to the render menu bar (next to Lights)
            physicsQMenu = QMenu("Physics", renderMenuBar)
            physicsQMenu.setToolTipsVisible(True)
            renderMenuBar.addMenu(physicsQMenu)
        else:
            # Fallback: use the top-level menu bar
            _log.info("  renderMenuBar not found, using top-level menu bar")
            physicsQMenu = mainWindow.menuBar().addMenu("Physics")
            physicsQMenu.setToolTipsVisible(True)

        # Simulate toggle (checkable)
        self._toggle_action = physicsQMenu.addAction(
            "Simulate", lambda: self._toggle_cmd.run())
        self._toggle_action.setCheckable(True)
        self._toggle_action.setChecked(False)

        # Grab mode toggle (checkable)
        self._grab_action = physicsQMenu.addAction(
            "Grab Mode", lambda: self._grab_cmd.run())
        self._grab_action.setCheckable(True)
        self._grab_action.setChecked(False)

        physicsQMenu.addSeparator()

        # Reset (plain action)
        self._reset_action = physicsQMenu.addAction(
            "Reset", lambda: self._reset_cmd.run())

        physicsQMenu.addSeparator()

        # Engine submenu with radio buttons
        engineQMenu = physicsQMenu.addMenu("Engine")
        self._engine_group = QActionGroup(engineQMenu)
        self._engine_group.setExclusive(True)

        if HAS_NEWTON:
            self._newton_action = engineQMenu.addAction(
                "Newton GPU", lambda: self._newton_cmd.run())
            self._newton_action.setCheckable(True)
            self._newton_action.setChecked(self._selected_engine == "newton")
            self._engine_group.addAction(self._newton_action)

        if HAS_PHYSX:
            self._physx_action = engineQMenu.addAction(
                "PhysX 5", lambda: self._physx_cmd.run())
            self._physx_action.setCheckable(True)
            self._physx_action.setChecked(self._selected_engine == "physx")
            self._engine_group.addAction(self._physx_action)

        _log.info(f"  menu items added (engine={self._selected_engine})")

    def _safeCall(self, fn, ctx, *args):
        try:
            _log.info(f"Menu action: {fn.__name__}")
            fn(ctx, *args)
        except Exception:
            _log.error(f"Error in {fn.__name__}:\n{traceback.format_exc()}")

    # ------------------------------------------------------------------
    # Commands
    # ------------------------------------------------------------------

    def _selectEngine(self, usdviewApi, engine_name):
        if self._running:
            _log.info("Stop physics before switching engine.")
            # Revert the radio button to current selection
            if engine_name == "newton" and hasattr(self, '_newton_action'):
                self._newton_action.setChecked(self._selected_engine == "newton")
            if engine_name == "physx" and hasattr(self, '_physx_action'):
                self._physx_action.setChecked(self._selected_engine == "physx")
            return

        self._selected_engine = engine_name
        _log.info(f"Engine selected: {self._selected_engine}")

    def _togglePhysics(self, usdviewApi):
        if not self._selected_engine:
            _log.info("No physics engine available!")
            return
        if self._selected_engine == "newton" and not HAS_NEWTON:
            _log.info("Newton not installed (pip install newton)")
            return
        if self._selected_engine == "physx" and not HAS_PHYSX:
            _log.info("ovphysx not installed")
            return
        if not HAS_HDEXEC:
            _log.info("pxr.HdExec not available")
            return

        if self._running:
            self._stopSim()
            if self._toggle_action:
                self._toggle_action.setChecked(False)
        else:
            self._startSim(usdviewApi)
            if self._toggle_action:
                self._toggle_action.setChecked(True)

    def _toggleGrab(self, usdviewApi):
        if self._event_filter:
            self._removeEventFilter(usdviewApi)
            if self._grab_action:
                self._grab_action.setChecked(False)
            _log.info("Grab mode disabled.")
        else:
            if not self._provider:
                _log.info("Start physics first.")
                if self._grab_action:
                    self._grab_action.setChecked(False)
                return
            self._installEventFilter(usdviewApi)
            if self._grab_action:
                self._grab_action.setChecked(True)
            _log.info("Grab mode enabled — shift+left-click-drag to grab bodies.")

    def _resetPhysics(self, usdviewApi):
        body_paths = []
        if self._provider and hasattr(self, '_body_map') and self._body_map:
            body_paths = list(self._body_map.keys())

        # Remove grab event filter before teardown
        if self._event_filter:
            self._removeEventFilter(usdviewApi)
            if self._grab_action:
                self._grab_action.setChecked(False)

        # Clear grab state
        self._grab_body_idx = -1

        self._stopSim()
        if self._provider:
            self._provider.release()
            self._provider = None

        if HAS_HDEXEC and body_paths:
            dummy = [(p, Gf.Matrix4d(1.0)) for p in body_paths]
            HdExec.SetCachedTransforms(dummy)
            HdExec.AdvanceGlobalTime(0.0)
            HdExec.ClearAllCachedTransforms()

        if self._stageView:
            self._stageView.updateGL()

        if self._toggle_action:
            self._toggle_action.setChecked(False)
        _log.info("Simulation reset.")

    # ------------------------------------------------------------------
    # Simulation
    # ------------------------------------------------------------------

    def _startSim(self, usdviewApi):
        try:
            self._startSimInner(usdviewApi)
        except Exception:
            _log.error("Failed to start sim:\n%s", traceback.format_exc())

    def _startSimInner(self, usdviewApi):
        stage = usdviewApi.stage
        root_layer = stage.GetRootLayer()
        scene_path = root_layer.identifier

        # Create provider
        if self._selected_engine == "newton":
            self._provider = NewtonProvider()
        else:
            self._provider = PhysXProvider()

        _log.info(f"Starting {self._provider.name}...")
        self._body_map = self._provider.initialize(scene_path, stage, device='gpu')

        if not self._body_map:
            _log.info("No physics bodies found.")
            self._provider = None
            return

        # Detect axis convention
        up_axis = UsdGeom.GetStageUpAxis(stage)
        # Only swap axes if provider needs it (Newton = Z-up internally)
        # PhysX respects the stage upAxis natively, no conversion needed.
        self._swap_yz = (up_axis == UsdGeom.Tokens.y) and self._provider.needs_axis_swap
        meters_per_unit = UsdGeom.GetStageMetersPerUnit(stage)
        self._grab_threshold = 2.0 / meters_per_unit

        if self._swap_yz:
            self._q_undo = Gf.Quatd(
                math.cos(-math.pi / 4), math.sin(-math.pi / 4), 0, 0)
            self._q_to_newton = Gf.Quatd(
                math.cos(math.pi / 4), math.sin(math.pi / 4), 0, 0)
        else:
            self._q_undo = None
            self._q_to_newton = None

        _log.info(f"upAxis={up_axis}, swap_yz={self._swap_yz}, "
                  f"grab_threshold={self._grab_threshold:.1f}")

        # Timing
        fps = stage.GetTimeCodesPerSecond()
        if fps <= 0:
            fps = 60.0
        self._dt = 1.0 / fps
        self._time = 0.0
        self._fps = fps

        # Register with HdExec
        HdExec.SetGlobalStage(stage)

        # Start QTimer
        try:
            from PySide6 import QtCore
        except ImportError:
            from PySide2 import QtCore

        self._running = True
        self._stageView = usdviewApi._UsdviewApi__appController._stageView
        self._sim_timer = QtCore.QTimer()
        self._sim_timer.timeout.connect(lambda: self._simStep())
        interval_ms = max(1, int(self._dt * 1000))
        self._sim_timer.start(interval_ms)

        _log.info(f"Running {self._provider.name} — "
                  f"{len(self._body_map)} bodies at {fps}fps")

    def _stopSim(self):
        self._running = False
        if self._sim_timer:
            self._sim_timer.stop()
            self._sim_timer = None
        if HAS_HDEXEC:
            HdExec.ClearAllCachedTransforms()

    def _simStep(self):
        try:
            self._simStepInner()
        except Exception:
            _log.error("simStep error:\n%s", traceback.format_exc())
            self._running = False

    def _simStepInner(self):
        if not self._running or not self._provider:
            return

        # Apply grab via provider
        if hasattr(self, '_grab_body_idx') and self._grab_body_idx >= 0:
            target_usd = self._grab_target_usd
            if self._swap_yz:
                target_sim = self._q_to_newton.Transform(target_usd)
            else:
                target_sim = target_usd
            self._provider.update_grab(
                (target_sim[0], target_sim[1], target_sim[2]))

        # Step physics
        self._provider.step(self._dt, self._time)
        self._time += self._dt

        # Get transforms and push to HdExec
        transforms = self._provider.get_body_transforms(self._swap_yz)
        HdExec.SetCachedTransforms(transforms)
        HdExec.AdvanceGlobalTime(self._time * self._fps)

        # Refresh viewport
        if self._stageView:
            self._stageView.updateGL()

    # ------------------------------------------------------------------
    # Grab mode (Qt event filter)
    # ------------------------------------------------------------------

    def _installEventFilter(self, usdviewApi):
        stageView = usdviewApi._UsdviewApi__appController._stageView
        self._event_filter = _GrabEventFilter(stageView, self, usdviewApi)
        stageView.installEventFilter(self._event_filter)
        stageView.setMouseTracking(True)

    def _removeEventFilter(self, usdviewApi):
        if self._event_filter:
            stageView = usdviewApi._UsdviewApi__appController._stageView
            stageView.removeEventFilter(self._event_filter)
            self._event_filter = None

    def _screenToRay(self, usdviewApi, x, y):
        stageView = usdviewApi._UsdviewApi__appController._stageView
        gfCamera, _ = stageView.resolveCamera()
        frustum = gfCamera.frustum

        w = stageView.width()
        h = stageView.height()
        ndc = Gf.Vec2d(
            (x / w) * 2.0 - 1.0,
            -((y / h) * 2.0 - 1.0))

        ray = frustum.ComputePickRay(ndc)
        origin = Gf.Vec3f(
            float(ray.startPoint[0]),
            float(ray.startPoint[1]),
            float(ray.startPoint[2]))
        direction = Gf.Vec3f(
            float(ray.direction[0]),
            float(ray.direction[1]),
            float(ray.direction[2]))
        return origin, direction

    def beginGrab(self, usdviewApi, x, y):
        if not self._provider:
            return False

        origin, direction = self._screenToRay(usdviewApi, x, y)

        # Find closest body to ray in USD space
        transforms = self._provider.get_body_transforms(self._swap_yz)

        ray_o = Gf.Vec3d(origin[0], origin[1], origin[2])
        ray_d = Gf.Vec3d(direction[0], direction[1], direction[2]).GetNormalized()

        best_dist = 1e10
        best_idx = -1
        best_path = None
        best_t = 0.0

        for path, M in transforms:
            pos_usd = Gf.Vec3d(M[3][0], M[3][1], M[3][2])
            v = pos_usd - ray_o
            t = Gf.Dot(v, ray_d)
            if t < 0:
                continue
            closest = ray_o + ray_d * t
            dist = (pos_usd - closest).GetLength()
            if dist < best_dist:
                best_dist = dist
                best_idx = self._body_map[path]
                best_path = path
                best_t = t

        if best_dist > self._grab_threshold or best_idx < 0:
            _log.info(f"No body hit (closest={best_dist:.2f})")
            return False

        # Initialize grab state
        self._grab_body_idx = best_idx
        self._grab_body_path = best_path
        self._grab_depth = best_t

        # Start target at body's current position in USD space
        for path, M in transforms:
            if path == best_path:
                self._grab_target_usd = Gf.Vec3d(M[3][0], M[3][1], M[3][2])
                break

        # Convert to sim space and notify provider
        if self._swap_yz:
            target_sim = self._q_to_newton.Transform(self._grab_target_usd)
        else:
            target_sim = self._grab_target_usd
        self._provider.begin_grab(
            best_idx, (target_sim[0], target_sim[1], target_sim[2]))

        _log.info(f"Grabbed {best_path} (idx={best_idx}, "
                  f"dist={best_dist:.2f}, depth={best_t:.2f})")
        return True

    def updateGrab(self, usdviewApi, x, y):
        if not hasattr(self, '_grab_body_idx') or self._grab_body_idx < 0:
            return
        origin, direction = self._screenToRay(usdviewApi, x, y)
        ray_o = Gf.Vec3d(origin[0], origin[1], origin[2])
        ray_d = Gf.Vec3d(direction[0], direction[1], direction[2]).GetNormalized()
        self._grab_target_usd = ray_o + ray_d * self._grab_depth

    def endGrab(self):
        if hasattr(self, '_grab_body_idx') and self._grab_body_idx >= 0:
            _log.info(f"Released {self._grab_body_path}")
            self._grab_body_idx = -1
            self._grab_body_path = None
            if self._provider:
                self._provider.end_grab()


# ======================================================================
# Qt Event Filter for Grab
# ======================================================================

try:
    from PySide6 import QtCore
except ImportError:
    from PySide2 import QtCore


class _GrabEventFilter(QtCore.QObject):
    """Intercepts mouse events for physics grabbing.
    Shift+left-click or middle-click to grab.
    """

    def __init__(self, stageView, plugin, usdviewApi):
        super().__init__(stageView)
        self._plugin = plugin
        self._api = usdviewApi
        self._grabbing = False

    def eventFilter(self, obj, event):
        etype = event.type()

        if etype == QtCore.QEvent.MouseButtonPress:
            btn = event.button()
            mods = event.modifiers()
            grab = False
            if btn == QtCore.Qt.LeftButton and (mods & QtCore.Qt.ShiftModifier):
                grab = True
            elif btn == QtCore.Qt.MiddleButton and not (mods & QtCore.Qt.AltModifier):
                grab = True

            if grab:
                x = event.x() * obj.devicePixelRatioF()
                y = event.y() * obj.devicePixelRatioF()
                if self._plugin.beginGrab(self._api, x, y):
                    self._grabbing = True
                    return True

        elif etype == QtCore.QEvent.MouseMove:
            if self._grabbing:
                x = event.x() * obj.devicePixelRatioF()
                y = event.y() * obj.devicePixelRatioF()
                self._plugin.updateGrab(self._api, x, y)
                return True

        elif etype == QtCore.QEvent.MouseButtonRelease:
            if self._grabbing:
                self._plugin.endGrab()
                self._grabbing = False
                return True

        return False


Tf.Type.Define(NewtonPhysicsPlugin)

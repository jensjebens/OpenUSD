# Newton GPU interactive physics plugin for UsdView.
#
# Right-click on a physics body to grab it. Drag to move. Release to throw.
# Uses Newton GPU's built-in Picking class (GPU raycast + spring-damper).
#
# The plugin also runs physics simulation in real-time, updating body
# transforms each frame via the HdExec TransformProvider pipeline.
#
# Installation:
#   export PXR_PLUGINPATH_NAME=/path/to/usdviewPlugin:$PXR_PLUGINPATH_NAME
#   usdview scene.usda

import os
import sys
import logging
import traceback

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

from pxr import Tf, Sdf, Gf, Usd, UsdGeom, UsdPhysics

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

from pxr.Usdviewq.plugin import PluginContainer


class NewtonPhysicsPlugin(PluginContainer):
    """UsdView plugin for interactive Newton GPU physics."""

    def registerPlugins(self, plugRegistry, plugCtx):
        _log.info("registerPlugins called!")
        self._ctx = plugCtx
        self._engine = None
        self._picking = None
        self._running = False
        self._sim_thread = None
        self._event_filter = None

        self._toggle_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.togglePhysics",
            "Start Physics",
            lambda ctx: self._safeCall(self._togglePhysics, ctx))

        self._grab_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.toggleGrab",
            "Enable Grab Mode",
            lambda ctx: self._safeCall(self._toggleGrab, ctx))

        self._reset_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.resetPhysics",
            "Reset Simulation",
            lambda ctx: self._safeCall(self._resetPhysics, ctx))

    def configureView(self, plugRegistry, plugUIBuilder):
        _log.info("configureView called!")
        menu = plugUIBuilder.findOrCreateMenu("Physics")
        self._toggle_action = menu.addItem(self._toggle_cmd)
        self._grab_action = menu.addItem(self._grab_cmd)
        self._reset_action = menu.addItem(self._reset_cmd)
        _log.info("  menu items added")

    def _safeCall(self, fn, ctx):
        try:
            _log.info(f"Menu action: {fn.__name__}")
            fn(ctx)
        except Exception:
            _log.error(f"Error in {fn.__name__}:\n{traceback.format_exc()}")

    # ------------------------------------------------------------------
    # Commands
    # ------------------------------------------------------------------

    def _togglePhysics(self, usdviewApi):
        if not HAS_NEWTON:
            _log.info("Error: newton not installed. pip install newton")
            return
        if not HAS_HDEXEC:
            _log.info("Error: pxr.HdExec not available.")
            return

        if self._running:
            self._stopSim()
            if hasattr(self, '_toggle_action') and self._toggle_action:
                self._toggle_action.setText("Start Physics")
        else:
            self._startSim(usdviewApi)
            if hasattr(self, '_toggle_action') and self._toggle_action:
                self._toggle_action.setText("Stop Physics")

    def _toggleGrab(self, usdviewApi):
        if self._event_filter:
            self._removeEventFilter(usdviewApi)
            if hasattr(self, '_grab_action') and self._grab_action:
                self._grab_action.setText("Enable Grab Mode")
            _log.info("Grab mode disabled.")
        else:
            if not self._engine:
                _log.info("Start physics first.")
                return
            self._installEventFilter(usdviewApi)
            if hasattr(self, '_grab_action') and self._grab_action:
                self._grab_action.setText("Disable Grab Mode")
            _log.info("Grab mode enabled — shift+left-click-drag to grab bodies.")

    def _resetPhysics(self, usdviewApi):
        # Capture body paths before teardown.
        body_paths = []
        if self._engine:
            body_paths = list(self._engine["body_map"].keys())

        self._stopSim()
        if self._engine:
            self._engine = None
            self._picking = None

        # After _stopSim clears the transform cache, we need to dirty
        # the physics prims so Hydra re-pulls GetPrim() and falls back
        # to the original USD transforms.  Momentarily write identity
        # matrices into the cache just to trigger the dirty pass.
        if HAS_HDEXEC and body_paths:
            dummy = [(p, Gf.Matrix4d(1.0)) for p in body_paths]
            HdExec.SetCachedTransforms(dummy)
            HdExec.AdvanceGlobalTime(0.0)
            HdExec.ClearAllCachedTransforms()

        # Repaint to show restored original poses.
        if hasattr(self, '_stageView') and self._stageView:
            self._stageView.updateGL()

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
        device = "cuda:0" if wp.is_cuda_available() else "cpu"

        _log.info(f"Initializing on {device}...")

        # Open a separate stage for Newton to parse — using the same
        # stage that UsdView owns can crash inside LoadUsdPhysicsFromRange
        # due to concurrent Hydra/ExecUsd access.
        root_layer = stage.GetRootLayer()
        newton_stage = Usd.Stage.Open(root_layer.identifier)
        _log.info(f"Opened separate stage for Newton: {root_layer.identifier}")

        builder = newton.ModelBuilder()
        builder.add_usd(newton_stage)
        model = builder.finalize(device=device)

        # Body path mapping.
        body_map = {}
        for idx, label in enumerate(builder.body_label):
            body_map[Sdf.Path(label)] = idx
            _log.info(f"  Body {idx}: {label}")

        if not body_map:
            _log.info("No physics bodies found.")
            return

        # Solver — default XPBD.
        solver = newton.solvers.SolverXPBD(model)
        states = [model.state(), model.state()]

        # Picking.
        self._picking = newton._src.viewer.picking.Picking(model)

        fps = stage.GetTimeCodesPerSecond()
        if fps <= 0:
            fps = 60.0
        dt = 1.0 / fps

        self._engine = {
            "model": model,
            "solver": solver,
            "states": states,
            "current": 0,
            "body_map": body_map,
            "dt": dt,
            "time": 0.0,
            "stage": stage,
            "fps": fps,
        }

        # Register with HdExec so the scene index knows about our stage.
        HdExec.SetGlobalStage(stage)

        # Start a QTimer on the main thread to step physics and refresh.
        # (Background threads can't trigger GL repaints.)
        try:
            from PySide6 import QtCore
        except ImportError:
            from PySide2 import QtCore

        self._running = True
        self._stageView = usdviewApi._UsdviewApi__appController._stageView
        self._sim_timer = QtCore.QTimer()
        self._sim_timer.timeout.connect(lambda: self._simStep())
        interval_ms = max(1, int(dt * 1000))
        self._sim_timer.start(interval_ms)

        _log.info(f"Running — {len(body_map)} bodies at {fps}fps")

    def _stopSim(self):
        self._running = False
        if hasattr(self, '_sim_timer') and self._sim_timer:
            self._sim_timer.stop()
            self._sim_timer = None
        if HAS_HDEXEC:
            HdExec.ClearAllCachedTransforms()

    def _simStep(self):
        """Called by QTimer on the main thread — step physics + refresh."""
        try:
            self._simStepInner()
        except Exception:
            _log.error("simStep error:\n%s", traceback.format_exc())
            self._running = False

    def _simStepInner(self):
        if not self._running or not self._engine:
            return

        e = self._engine
        cur = e["current"]
        nxt = 1 - cur

        # Apply picking forces.
        if self._picking and self._picking.is_picking():
            self._picking._apply_picking_force(e["states"][cur])

        # Step.
        contacts = e["model"].collide(e["states"][cur])
        e["solver"].step(
            e["states"][cur], e["states"][nxt],
            control=None, contacts=contacts, dt=e["dt"])
        e["current"] = nxt
        e["time"] += e["dt"]

        # Cache the numpy readback and push transforms to HdExec's C++
        # cache.  This avoids Python callbacks from TBB threads entirely.
        body_q = e["states"][nxt].body_q.numpy()

        transforms = []
        for path, idx in e["body_map"].items():
            tf = body_q[idx]
            px, py, pz = float(tf[0]), float(tf[1]), float(tf[2])
            qx, qy, qz, qw = float(tf[3]), float(tf[4]), float(tf[5]), float(tf[6])

            quat = Gf.Quatd(qw, qx, qy, qz)
            mat = Gf.Matrix4d()
            mat.SetRotate(quat)
            mat.SetTranslateOnly(Gf.Vec3d(px, py, pz))
            transforms.append((path, mat))

        HdExec.SetCachedTransforms(transforms)

        # Log every ~1 second
        if int(e["time"] * 24) % 24 == 0:
            tf = body_q[0] if len(body_q) > 0 else [0,0,0,0,0,0,1]
            _log.info(f"Step t={e['time']:.2f}s pos=({tf[0]:.2f}, {tf[1]:.2f}, {tf[2]:.2f})")

        # Dirty the HdExec scene index so Hydra re-pulls transforms.
        # Advance global time so the TransformProvider sees the new frame.
        HdExec.AdvanceGlobalTime(e["time"] * e.get("fps", 24.0))

        # Refresh viewport.  Release the GIL first so that Hydra's TBB
        # worker threads can call back into the Python TransformProvider
        # without deadlocking on the GIL.
        if self._stageView:
            self._stageView.updateGL()

    # ------------------------------------------------------------------
    # Qt event filter for grab mode
    # ------------------------------------------------------------------

    def _installEventFilter(self, usdviewApi):
        stageView = usdviewApi._UsdviewApi__appController._stageView
        self._event_filter = _GrabEventFilter(
            stageView, self, usdviewApi)
        stageView.installEventFilter(self._event_filter)
        # Enable mouse tracking so we get move events without buttons held.
        stageView.setMouseTracking(True)

    def _removeEventFilter(self, usdviewApi):
        if self._event_filter:
            stageView = usdviewApi._UsdviewApi__appController._stageView
            stageView.removeEventFilter(self._event_filter)
            self._event_filter = None

    def _screenToRay(self, usdviewApi, x, y):
        """Convert screen coordinates to a world-space ray."""
        stageView = usdviewApi._UsdviewApi__appController._stageView
        gfCamera, _ = stageView.resolveCamera()
        frustum = gfCamera.frustum

        # Normalize to [-1, 1].
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
        """Start grabbing a body at screen position (x, y)."""
        if not self._picking or not self._engine:
            return False

        origin, direction = self._screenToRay(usdviewApi, x, y)

        cur = self._engine["current"]
        state = self._engine["states"][cur]
        self._picking.pick(
            state,
            wp.vec3f(origin[0], origin[1], origin[2]),
            wp.vec3f(direction[0], direction[1], direction[2]))

        if self._picking.is_picking():
            _log.info("Grabbed body!")
            return True
        return False

    def updateGrab(self, usdviewApi, x, y):
        """Update grab target during drag."""
        if not self._picking or not self._picking.is_picking():
            return
        origin, direction = self._screenToRay(usdviewApi, x, y)
        self._picking.update(
            wp.vec3f(origin[0], origin[1], origin[2]),
            wp.vec3f(direction[0], direction[1], direction[2]))

    def endGrab(self):
        """Release the grabbed body."""
        if self._picking and self._picking.is_picking():
            self._picking.release()
            _log.info("Released body.")


# Qt event filter
try:
    from PySide6 import QtCore
except ImportError:
    from PySide2 import QtCore


class _GrabEventFilter(QtCore.QObject):
    """Intercepts mouse events for physics grabbing when grab mode is on.

    Uses Shift+left-click to match Omniverse convention. Falls back to
    middle-click if Shift+left is consumed by UsdView's multi-select.
    """

    def __init__(self, stageView, plugin, usdviewApi):
        super().__init__(stageView)
        self._plugin = plugin
        self._api = usdviewApi
        self._grabbing = False
        _log.info(f"Event filter installed on {stageView.__class__.__name__}")

    def eventFilter(self, obj, event):
        etype = event.type()

        if etype == QtCore.QEvent.MouseButtonPress:
            btn = event.button()
            mods = event.modifiers()
            # Shift+left or plain middle-click
            grab = False
            if btn == QtCore.Qt.LeftButton and (mods & QtCore.Qt.ShiftModifier):
                grab = True
            elif btn == QtCore.Qt.MiddleButton and not (mods & QtCore.Qt.AltModifier):
                grab = True

            if grab:
                x = event.x() * obj.devicePixelRatioF()
                y = event.y() * obj.devicePixelRatioF()
                _log.info(f"Grab attempt at ({x:.0f}, {y:.0f})")
                if self._plugin.beginGrab(self._api, x, y):
                    self._grabbing = True
                    return True
                else:
                    _log.info("No body hit")

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

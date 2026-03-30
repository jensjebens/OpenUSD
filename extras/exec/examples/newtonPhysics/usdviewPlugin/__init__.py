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
import time
import threading

from pxr import Tf, Sdf, Gf, Usd, UsdPhysics

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
        self._ctx = plugCtx
        self._engine = None
        self._picking = None
        self._running = False
        self._sim_thread = None
        self._event_filter = None

        self._toggle_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.togglePhysics",
            "Start/Stop Physics",
            self._togglePhysics)

        self._grab_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.toggleGrab",
            "Enable/Disable Grab Mode",
            self._toggleGrab)

        self._reset_cmd = plugRegistry.registerCommandPlugin(
            "NewtonPhysicsPlugin.resetPhysics",
            "Reset Simulation",
            self._resetPhysics)

    def configureView(self, plugRegistry, plugUIBuilder):
        menu = plugUIBuilder.findOrCreateMenu("Physics")
        menu.addItem(self._toggle_cmd)
        menu.addItem(self._grab_cmd)
        menu.addItem(self._reset_cmd)

    # ------------------------------------------------------------------
    # Commands
    # ------------------------------------------------------------------

    def _togglePhysics(self, usdviewApi):
        if not HAS_NEWTON:
            print("[Newton] Error: newton not installed. pip install newton")
            return
        if not HAS_HDEXEC:
            print("[Newton] Error: pxr.HdExec not available.")
            return

        if self._running:
            self._stopSim()
        else:
            self._startSim(usdviewApi)

    def _toggleGrab(self, usdviewApi):
        if self._event_filter:
            self._removeEventFilter(usdviewApi)
            print("[Newton] Grab mode disabled.")
        else:
            if not self._engine:
                print("[Newton] Start physics first.")
                return
            self._installEventFilter(usdviewApi)
            print("[Newton] Grab mode enabled — right-click-drag to grab bodies.")

    def _resetPhysics(self, usdviewApi):
        self._stopSim()
        if self._engine:
            self._engine = None
            self._picking = None
        print("[Newton] Simulation reset.")

    # ------------------------------------------------------------------
    # Simulation
    # ------------------------------------------------------------------

    def _startSim(self, usdviewApi):
        stage = usdviewApi.stage
        device = "cuda:0" if wp.is_cuda_available() else "cpu"

        print(f"[Newton] Initializing on {device}...")

        builder = newton.ModelBuilder()
        builder.add_usd(stage)
        model = builder.finalize(device=device)

        # Body path mapping.
        body_map = {}
        for idx, label in enumerate(builder.body_label):
            body_map[Sdf.Path(label)] = idx

        if not body_map:
            print("[Newton] No physics bodies found.")
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
        }

        # Register TransformProvider.
        HdExec.RegisterTransformProvider("newtonGPU", self._provider)
        HdExec.SetGlobalStage(stage)

        # Start simulation loop in background thread.
        self._running = True
        self._sim_thread = threading.Thread(
            target=self._simLoop, args=(usdviewApi,), daemon=True)
        self._sim_thread.start()

        print(f"[Newton] Running — {len(body_map)} bodies at {fps}fps")

    def _stopSim(self):
        self._running = False
        if self._sim_thread:
            self._sim_thread.join(timeout=2.0)
            self._sim_thread = None
        if HAS_HDEXEC:
            HdExec.UnregisterTransformProvider("newtonGPU")

    def _simLoop(self, usdviewApi):
        """Background thread: step physics and trigger viewport redraw."""
        while self._running and self._engine:
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

            # Trigger viewport redraw (from main thread via Qt).
            try:
                stageView = usdviewApi._UsdviewApi__appController._stageView
                stageView.update()
            except Exception:
                pass

            time.sleep(e["dt"] * 0.8)  # Slightly faster than real-time

    def _provider(self, prim_path, time_seconds):
        """TransformProvider callback — returns body transforms."""
        if not self._engine:
            return None

        e = self._engine
        idx = e["body_map"].get(prim_path)
        if idx is None:
            return None

        cur = e["current"]
        tf = e["states"][cur].body_q.numpy()[idx]
        px, py, pz = float(tf[0]), float(tf[1]), float(tf[2])
        qx, qy, qz, qw = float(tf[3]), float(tf[4]), float(tf[5]), float(tf[6])

        quat = Gf.Quatd(qw, qx, qy, qz)
        mat = Gf.Matrix4d()
        mat.SetRotate(quat)
        mat.SetTranslateOnly(Gf.Vec3d(px, py, pz))
        return mat

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

        ray = frustum.ComputeRay(ndc)
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
            print("[Newton] Grabbed body!")
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
            print("[Newton] Released body.")


# Qt event filter
try:
    from PySide6 import QtCore
except ImportError:
    from PySide2 import QtCore


class _GrabEventFilter(QtCore.QObject):
    """Intercepts right-click-drag for physics grabbing."""

    def __init__(self, stageView, plugin, usdviewApi):
        super().__init__(stageView)
        self._plugin = plugin
        self._api = usdviewApi
        self._grabbing = False

    def eventFilter(self, obj, event):
        if event.type() == QtCore.QEvent.MouseButtonPress:
            if event.button() == QtCore.Qt.RightButton:
                mods = event.modifiers()
                # Don't intercept Alt+right (camera zoom).
                if not (mods & (QtCore.Qt.AltModifier | QtCore.Qt.MetaModifier)):
                    x = event.x() * obj.devicePixelRatioF()
                    y = event.y() * obj.devicePixelRatioF()
                    if self._plugin.beginGrab(self._api, x, y):
                        self._grabbing = True
                        return True

        elif event.type() == QtCore.QEvent.MouseMove:
            if self._grabbing:
                x = event.x() * obj.devicePixelRatioF()
                y = event.y() * obj.devicePixelRatioF()
                self._plugin.updateGrab(self._api, x, y)
                return True

        elif event.type() == QtCore.QEvent.MouseButtonRelease:
            if event.button() == QtCore.Qt.RightButton and self._grabbing:
                self._plugin.endGrab()
                self._grabbing = False
                return True

        return False


Tf.Type.Define(NewtonPhysicsPlugin)

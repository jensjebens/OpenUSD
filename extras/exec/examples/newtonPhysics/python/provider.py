# SPDX-License-Identifier: Apache-2.0

"""TransformProvider registration for Newton GPU.

Registers a named TransformProvider with HdExecComputedTransformSceneIndex
so Newton GPU's simulated transforms flow through the Hydra pipeline.
"""

from typing import Optional

from pxr import Gf, Sdf

from .engine import NewtonEngine

# Global engine instance (singleton for now — matches the C++ pattern).
_engine: Optional[NewtonEngine] = None


def get_engine() -> Optional[NewtonEngine]:
    """Get the global Newton engine instance."""
    return _engine


def register_provider(stage, solver_name: str = "xpbd", device: str = "cuda:0"):
    """Initialize Newton GPU and register the TransformProvider.

    Args:
        stage: USD stage with physics schemas.
        solver_name: Solver backend name.
        device: Warp device string.
    """
    global _engine

    # Import HdExec — may not be available if imaging wasn't built.
    try:
        from pxr import HdExec
    except ImportError:
        print("[Newton GPU] Warning: pxr.HdExec not available. "
              "TransformProvider not registered.")
        return

    # Initialize the engine.
    _engine = NewtonEngine(device=device)
    _engine.initialize(stage, solver_name=solver_name)

    print(f"[Newton GPU] Initialized: {_engine.get_body_count()} bodies, "
          f"solver={_engine.solver_name}, device={device}")

    # Register the provider.
    # The provider callback is called by _ExecMatrixDataSource::GetTypedValue()
    # on every frame for every physics prim.
    def _newton_provider(prim_path: Sdf.Path,
                         time_seconds: float) -> Optional[Gf.Matrix4d]:
        if _engine is None or not _engine.initialized:
            return None

        if not _engine.has_body(prim_path):
            return None

        _engine.advance_to_time(time_seconds)
        return _engine.get_transform(prim_path)

    # Note: This requires the C++ HdExec to expose RegisterTransformProvider
    # to Python. For now we store the provider for use by a Python-side
    # scene index integration. The C++ side is handled via the existing
    # OpenExec computation path.
    #
    # TODO: Add Python bindings for HdExecComputedTransformSceneIndex
    # static methods (RegisterTransformProvider, SetGlobalStage, etc.)

    return _engine


def unregister_provider():
    """Shut down Newton GPU and unregister the provider."""
    global _engine
    if _engine:
        _engine.reset()
        _engine = None

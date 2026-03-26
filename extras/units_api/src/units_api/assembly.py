"""Assembly-time corrective transforms at unit boundaries."""

from pxr import Usd, UsdGeom, Gf
from .metrics_api import MetricsAPI


class MetricsAssembler:
    """Non-destructive corrective transforms at unit reference boundaries."""

    @staticmethod
    def compute_corrective_scale(source_mpu: float, target_mpu: float) -> float:
        """Return source_mpu / target_mpu."""
        return source_mpu / target_mpu

    @staticmethod
    def compute_corrective_rotation(source_up: str, target_up: str):
        """Return rotation angle in degrees around X, or None if axes match.

        Y→Z: -90°  Z→Y: +90°
        """
        if source_up == target_up:
            return None
        if source_up == "Y" and target_up == "Z":
            return -90.0
        if source_up == "Z" and target_up == "Y":
            return 90.0
        return None

    @staticmethod
    def apply_corrective_xform(
        prim,
        source_mpu: float,
        target_mpu: float,
        source_up: str = None,
        target_up: str = None,
    ) -> dict:
        """Add corrective xformOps before existing ops on prim.

        Adds xformOp:scale:metricsCorrection and, if up axes differ,
        xformOp:rotateX:metricsCorrection.  Returns dict describing what was applied.
        """
        xformable = UsdGeom.Xformable(prim)
        existing_ops = xformable.GetOrderedXformOps()

        scale_val = MetricsAssembler.compute_corrective_scale(source_mpu, target_mpu)
        scale_op = xformable.AddScaleOp(
            UsdGeom.XformOp.PrecisionDouble, "metricsCorrection"
        )
        scale_op.Set(Gf.Vec3d(scale_val, scale_val, scale_val))

        new_ops = [scale_op]
        rotation = None

        if source_up is not None and target_up is not None:
            rotation = MetricsAssembler.compute_corrective_rotation(source_up, target_up)
            if rotation is not None:
                rot_op = xformable.AddRotateXOp(
                    UsdGeom.XformOp.PrecisionDouble, "metricsCorrection"
                )
                rot_op.Set(rotation)
                new_ops.append(rot_op)

        xformable.SetXformOpOrder(new_ops + existing_ops)

        return {
            "prim_path": str(prim.GetPath()),
            "source_mpu": source_mpu,
            "target_mpu": target_mpu,
            "scale": scale_val,
            "rotation": rotation,
        }

    @staticmethod
    def _parent_has_metrics_context(prim) -> bool:
        """Return True if any ancestor of prim has MetricsAPI applied."""
        current = prim.GetParent()
        while current.IsValid():
            if MetricsAPI.has_metrics(current):
                return True
            parent = current.GetParent()
            if not parent.IsValid():
                break
            current = parent
        return False

    @staticmethod
    def correct_reference_boundary(prim) -> "dict | None":
        """Compare prim's own metrics against parent's effective metrics.

        Applies a corrective xform if mpu or upAxis differ.
        Returns None if there is no mismatch or no parent metrics context.
        """
        if not MetricsAPI.has_metrics(prim):
            return None
        if not MetricsAssembler._parent_has_metrics_context(prim):
            return None

        own_metrics = MetricsAPI.get_metrics(prim)
        parent = prim.GetParent()
        parent_effective = MetricsAPI.get_effective_metrics(parent)

        own_mpu = own_metrics.get("metersPerUnit")
        parent_mpu = parent_effective.get("metersPerUnit")
        own_up = own_metrics.get("upAxis")
        parent_up = parent_effective.get("upAxis")

        mpu_mismatch = (
            own_mpu is not None
            and parent_mpu is not None
            and abs(own_mpu - parent_mpu) > 1e-9
        )
        up_mismatch = (
            own_up is not None
            and parent_up is not None
            and own_up != parent_up
        )

        if not mpu_mismatch and not up_mismatch:
            return None

        source_mpu = own_mpu if own_mpu is not None else parent_mpu
        target_mpu = parent_mpu
        source_up = own_up if up_mismatch else None
        target_up = parent_up if up_mismatch else None

        return MetricsAssembler.apply_corrective_xform(
            prim, source_mpu, target_mpu, source_up, target_up
        )

    @staticmethod
    def correct_stage(stage) -> list:
        """Traverse all prims and apply corrective xforms at reference boundaries."""
        corrections = []
        for prim in stage.Traverse():
            if MetricsAPI.has_metrics(prim):
                result = MetricsAssembler.correct_reference_boundary(prim)
                if result is not None:
                    corrections.append(result)
        return corrections

    @staticmethod
    def audit_stage(stage) -> list:
        """Dry-run version of correct_stage — report mismatches without applying."""
        mismatches = []
        for prim in stage.Traverse():
            if not MetricsAPI.has_metrics(prim):
                continue
            if not MetricsAssembler._parent_has_metrics_context(prim):
                continue

            own_metrics = MetricsAPI.get_metrics(prim)
            parent = prim.GetParent()
            parent_effective = MetricsAPI.get_effective_metrics(parent)

            own_mpu = own_metrics.get("metersPerUnit")
            parent_mpu = parent_effective.get("metersPerUnit")
            own_up = own_metrics.get("upAxis")
            parent_up = parent_effective.get("upAxis")

            mpu_mismatch = (
                own_mpu is not None
                and parent_mpu is not None
                and abs(own_mpu - parent_mpu) > 1e-9
            )
            up_mismatch = (
                own_up is not None
                and parent_up is not None
                and own_up != parent_up
            )

            if not mpu_mismatch and not up_mismatch:
                continue

            source_mpu = own_mpu if own_mpu is not None else parent_mpu
            target_mpu = parent_mpu
            source_up = own_up if up_mismatch else None
            target_up = parent_up if up_mismatch else None
            rotation = None
            if source_up is not None and target_up is not None:
                rotation = MetricsAssembler.compute_corrective_rotation(source_up, target_up)
            scale_val = MetricsAssembler.compute_corrective_scale(source_mpu, target_mpu)

            mismatches.append({
                "prim_path": str(prim.GetPath()),
                "source_mpu": source_mpu,
                "target_mpu": target_mpu,
                "scale": scale_val,
                "rotation": rotation,
            })
        return mismatches

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
    def bake_to_units(
        stage,
        target_mpu: float = 1.0,
        target_kpu: float = 1.0,
        edit_target=None,
    ) -> dict:
        """Bake all unit-bearing attribute values as overs in target units.

        Creates an override layer (or uses the provided one), sets it as the
        edit target, changes the stage metersPerUnit/kilogramsPerUnit to match
        the target, and writes converted values for every unit-bearing
        attribute on every prim.

        Unlike correct_stage() which only adds corrective xformOps at
        reference boundaries, bake_to_units() rewrites actual attribute
        values so downstream consumers see correct numbers without needing
        UnitsLens.

        Args:
            stage: The USD stage to bake.
            target_mpu: Target metersPerUnit (default 1.0 = meters).
            target_kpu: Target kilogramsPerUnit (default 1.0 = kilograms).
            edit_target: Optional Sdf.Layer to write overs into. If None,
                a new anonymous layer is created and inserted as the
                strongest sublayer.

        Returns:
            dict with keys:
                "layer": The Sdf.Layer containing the overs.
                "attrs_converted": Number of attributes converted.
                "prims_visited": Number of prims visited.
                "time_samples_converted": Number of individual time samples converted.
                "splines_converted": Number of splines converted.
        """
        from pxr import Sdf
        from .dimensions import get_dimension, conversion_factor, Dimension
        from .units_lens import UnitsLens, _apply_factor, _scale_spline

        _ZERO_DIM = Dimension(0, 0, 0)

        # --- Phase 1: Collect source metrics for every prim BEFORE changing anything ---
        prim_metrics = {}
        for prim in stage.Traverse():
            prim_metrics[str(prim.GetPath())] = MetricsAPI.get_effective_metrics(prim)

        # Clear cache since we're about to change the stage
        UnitsLens.clear_cache()

        # --- Phase 2: Create/insert override layer as the strongest sublayer ---
        # The override layer must be stronger than the root layer's own opinions.
        # We achieve this by making it a sublayer at position 0 and writing there.
        # However, for the overs to win over root-layer opinions, we need to
        # use the session layer or write overs to a layer that is consulted
        # before the root. The session layer is the strongest non-local layer.
        if edit_target is None:
            edit_target = Sdf.Layer.CreateAnonymous("baked_units.usda")
            # Insert as sublayer of the session layer (strongest position)
            session = stage.GetSessionLayer()
            session.subLayerPaths.insert(0, edit_target.identifier)

        edit_target.metersPerUnit = target_mpu
        if hasattr(edit_target, 'kilogramsPerUnit'):
            edit_target.kilogramsPerUnit = target_kpu

        stage.SetEditTarget(Usd.EditTarget(edit_target))

        stats = {
            "layer": edit_target,
            "attrs_converted": 0,
            "prims_visited": 0,
            "time_samples_converted": 0,
            "splines_converted": 0,
        }

        # --- Phase 3: Collect all values to convert BEFORE writing any overs ---
        # We must read all composed values before writing, because once we start
        # writing overs to the stronger session sublayer, subsequent reads would
        # see partially-converted data.
        conversions = []  # List of (prim_path, attr_name, type_name, value_or_samples, kind)

        for prim in stage.Traverse():
            stats["prims_visited"] += 1
            prim_path = str(prim.GetPath())
            metrics = prim_metrics.get(prim_path)
            if metrics is None:
                continue
            source_mpu = metrics["metersPerUnit"]
            source_kpu = metrics["kilogramsPerUnit"]

            for attr in prim.GetAttributes():
                if not attr.HasAuthoredValue():
                    continue

                dim = get_dimension(attr.GetName())
                attr_source_mpu = source_mpu
                attr_source_kpu = source_kpu

                if dim is None:
                    from .per_attribute import PerAttributeUnits, str_to_dimension
                    annotation = PerAttributeUnits.get_annotation(attr)
                    if annotation is not None:
                        dim = str_to_dimension(annotation["dimension"])
                        attr_source_mpu = annotation["metersPerUnit"]
                        attr_source_kpu = annotation["kilogramsPerUnit"]
                    else:
                        continue

                if dim == _ZERO_DIM:
                    continue

                factor = conversion_factor(
                    attr_source_mpu, target_mpu, dim,
                    attr_source_kpu, target_kpu,
                )
                if abs(factor - 1.0) < 1e-12:
                    continue

                # Handle splines
                if attr.HasSpline():
                    try:
                        spline = attr.GetSpline()
                        if not spline.IsEmpty():
                            converted_spline = _scale_spline(spline, factor)
                            conversions.append((
                                prim_path, attr.GetName(),
                                attr.GetTypeName(), converted_spline, "spline"
                            ))
                            continue
                    except Exception:
                        pass

                # Handle time samples
                time_samples = attr.GetTimeSamples()
                if time_samples:
                    samples = []
                    for t in time_samples:
                        val = attr.Get(t)
                        if val is not None:
                            samples.append((t, _apply_factor(val, factor)))
                    conversions.append((
                        prim_path, attr.GetName(),
                        attr.GetTypeName(), samples, "time_samples"
                    ))
                    continue

                # Handle default value
                val = attr.Get()
                if val is not None:
                    conversions.append((
                        prim_path, attr.GetName(),
                        attr.GetTypeName(), _apply_factor(val, factor), "default"
                    ))

        # --- Phase 4: Write all overs at once ---
        for prim_path, attr_name, type_name, data, kind in conversions:
            over_prim = stage.OverridePrim(prim_path)
            over_attr = over_prim.GetAttribute(attr_name)
            if not over_attr.IsValid():
                over_attr = over_prim.CreateAttribute(attr_name, type_name)

            if kind == "spline":
                over_attr.SetSpline(data)
                stats["splines_converted"] += 1
                stats["attrs_converted"] += 1
            elif kind == "time_samples":
                for t, val in data:
                    over_attr.Set(val, t)
                    stats["time_samples_converted"] += 1
                stats["attrs_converted"] += 1
            elif kind == "default":
                over_attr.Set(data)
                stats["attrs_converted"] += 1

        # Clear UnitsLens cache since metrics context has changed
        UnitsLens.clear_cache()

        return stats

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

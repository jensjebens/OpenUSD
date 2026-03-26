"""Units Lens — read/author USD attribute values with unit conversion."""

from pxr import Usd, Gf, Vt, UsdGeom

from .metrics_api import MetricsAPI
from .dimensions import get_dimension, conversion_factor, Dimension

_ZERO_DIM = Dimension(0, 0, 0)

# Optional numpy acceleration for large arrays
try:
    import numpy as np
    _HAS_NUMPY = True
except ImportError:
    _HAS_NUMPY = False


# ---------------------------------------------------------------------------
# Metrics cache — avoid repeated ancestor walks for the same prim
# ---------------------------------------------------------------------------

class _MetricsCache:
    """Simple path-keyed cache for effective metrics. Not thread-safe.
    
    Call clear() when the stage is edited (metrics may have changed).
    """
    def __init__(self):
        self._cache: dict[str, dict] = {}

    def get(self, prim: Usd.Prim) -> dict:
        key = str(prim.GetPath())
        if key not in self._cache:
            self._cache[key] = MetricsAPI.get_effective_metrics(prim)
        return self._cache[key]

    def clear(self):
        self._cache.clear()


# Module-level cache instance
_metrics_cache = _MetricsCache()


def _find_root_mpu(prim: Usd.Prim) -> float:
    """Return the metersPerUnit of the outermost ancestor with MetricsAPI (= world-space mpu)."""
    last_mpu = None
    current = prim
    while current.IsValid():
        metrics = MetricsAPI.get_metrics(current)
        if metrics.get("metersPerUnit") is not None:
            last_mpu = metrics["metersPerUnit"]
        parent = current.GetParent()
        if not parent.IsValid():
            break
        current = parent
    if last_mpu is None:
        return MetricsAPI._stage_or_default_metrics(prim.GetStage())["metersPerUnit"]
    return last_mpu


def _scale_matrix_translation(mat, factor: float):
    """Scale only the translation component of a 4x4 matrix.

    Rotation and scale components are unitless and must not be modified.
    USD uses row-major layout: row 3 contains (tx, ty, tz, 1).
    """
    result = Gf.Matrix4d(mat)
    t = result.ExtractTranslation()
    row3 = result.GetRow(3)
    result.SetRow(3, Gf.Vec4d(t[0] * factor, t[1] * factor, t[2] * factor, row3[3]))
    return result


def _apply_factor(val, factor: float):
    """Apply a scalar conversion factor to a USD attribute value.

    For matrices (GfMatrix4d, VtMatrix4dArray), only the translation
    component is scaled — rotation and scale are unitless.
    
    Uses numpy when available for VtArray types (93× faster on 100k elements).
    """
    if factor == 1.0:
        return val
    if isinstance(val, (int, float)):
        return type(val)(val * factor)
    # Gf vectors support scalar multiplication natively
    if isinstance(val, (Gf.Vec2f, Gf.Vec2d, Gf.Vec3f, Gf.Vec3d, Gf.Vec4f, Gf.Vec4d)):
        return val * factor
    # Matrices — scale translation only
    if isinstance(val, Gf.Matrix4d):
        return _scale_matrix_translation(val, factor)
    if isinstance(val, Gf.Matrix4f):
        m4d = Gf.Matrix4d(val)
        scaled = _scale_matrix_translation(m4d, factor)
        return Gf.Matrix4f(scaled)
    # Vt matrix arrays (e.g. UsdSkel restTransforms, bindTransforms)
    if isinstance(val, Vt.Matrix4dArray):
        return Vt.Matrix4dArray([_scale_matrix_translation(m, factor) for m in val])
    # Vt typed arrays — numpy fast-path when available
    if _HAS_NUMPY:
        if isinstance(val, (Vt.Vec3fArray, Vt.Vec3dArray, Vt.Vec2fArray,
                            Vt.FloatArray, Vt.DoubleArray)):
            arr = np.array(val)
            arr = arr * factor
            return type(val).FromNumpy(arr)
    # Python fallback for Vt arrays
    if isinstance(val, Vt.Vec3fArray):
        return Vt.Vec3fArray([v * factor for v in val])
    if isinstance(val, Vt.Vec3dArray):
        return Vt.Vec3dArray([v * factor for v in val])
    if isinstance(val, Vt.Vec2fArray):
        return Vt.Vec2fArray([v * factor for v in val])
    if isinstance(val, Vt.FloatArray):
        return Vt.FloatArray([v * factor for v in val])
    if isinstance(val, Vt.DoubleArray):
        return Vt.DoubleArray([v * factor for v in val])
    # Unknown type — pass through unchanged
    return val


def _scale_spline(spline, factor: float):
    """Scale a Ts.Spline: multiply knot values and tangent slopes by factor.

    Tangent slopes are dValue/dTime — since value scales, slope scales equally.
    Tangent widths are in time units and must NOT be scaled.
    Pre-values (dual-valued knots) also scale.
    """
    from pxr import Ts
    result = Ts.Spline()
    for time in spline.GetKnots().keys():
        src = spline.GetKnot(time)
        knot = Ts.Knot()
        knot.SetTime(time)
        knot.SetValue(src.GetValue() * factor)
        knot.SetNextInterpolation(src.GetNextInterpolation())
        # Tangent slopes scale with value
        knot.SetPreTanSlope(src.GetPreTanSlope() * factor)
        knot.SetPreTanWidth(src.GetPreTanWidth())  # time — no scale
        knot.SetPostTanSlope(src.GetPostTanSlope() * factor)
        knot.SetPostTanWidth(src.GetPostTanWidth())  # time — no scale
        # Dual-valued knots (pre-value)
        if src.IsDualValued():
            knot.SetPreValue(src.GetPreValue() * factor)
        result.SetKnot(knot)
    # Preserve extrapolation settings
    result.SetPreExtrapolation(spline.GetPreExtrapolation())
    result.SetPostExtrapolation(spline.GetPostExtrapolation())
    return result


class UnitsLens:
    """Unit-aware reading and authoring of USD attribute values."""

    @staticmethod
    def get_attr(attr: Usd.Attribute, target_mpu: float = 1.0, target_kpu: float = 1.0,
                 time=None):
        """Get an attribute value converted to target units.

        If the attribute has no authored value, returns None.
        If the attribute is not in the dimension registry, returns the raw value.
        If the attribute is dimensionless (Dimension(0,0,0)), returns the raw value.
        """
        if time is None:
            time = Usd.TimeCode.Default()

        val = attr.Get(time)
        if val is None:
            return None

        dim = get_dimension(attr.GetName())
        if dim is None:
            from .per_attribute import PerAttributeUnits
            annotation = PerAttributeUnits.get_annotation(attr)
            if annotation is not None:
                return PerAttributeUnits.get_attr(attr, target_mpu, target_kpu, time)
            return val
        if dim == _ZERO_DIM:
            return val

        prim = attr.GetPrim()
        metrics = _metrics_cache.get(prim)
        source_mpu = metrics["metersPerUnit"]
        source_kpu = metrics["kilogramsPerUnit"]

        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)
        return _apply_factor(val, factor)

    @staticmethod
    def set_attr(attr: Usd.Attribute, value, source_mpu: float = 1.0, source_kpu: float = 1.0,
                 time=None) -> bool:
        """Set a value expressed in source units, converting to the prim's native units.

        The inverse of get_attr(): value is in source units, stored in the prim's native units.

        Example: set a 5-meter distance on a cm-scale prim
            set_attr(attr, 5.0, source_mpu=1.0)  # prim is cm → stores 500.0
        """
        if time is None:
            time = Usd.TimeCode.Default()

        dim = get_dimension(attr.GetName())
        if dim is None:
            from .per_attribute import PerAttributeUnits
            annotation = PerAttributeUnits.get_annotation(attr)
            if annotation is not None:
                return PerAttributeUnits.set_attr(attr, value, source_mpu, source_kpu, time)
            return attr.Set(value, time)
        if dim == _ZERO_DIM:
            return attr.Set(value, time)

        prim = attr.GetPrim()
        metrics = _metrics_cache.get(prim)
        target_mpu = metrics["metersPerUnit"]
        target_kpu = metrics["kilogramsPerUnit"]

        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)
        converted = _apply_factor(value, factor)
        return attr.Set(converted, time)

    @staticmethod
    def get_in_meters(attr: Usd.Attribute, time=None):
        """Convenience: get attribute value converted to meters."""
        return UnitsLens.get_attr(attr, target_mpu=1.0, time=time)

    # ------------------------------------------------------------------
    # Time-sampled bulk access
    # ------------------------------------------------------------------

    @staticmethod
    def get_time_samples(attr: Usd.Attribute, target_mpu: float = 1.0,
                         target_kpu: float = 1.0) -> list[tuple[float, object]]:
        """Get ALL time samples for an attribute, each converted to target units.

        Returns list of (time, converted_value) tuples.
        Resolves the conversion factor once and applies it to every sample.
        Much faster than calling get_attr() in a loop (avoids repeated ancestor walks).
        
        Returns empty list if no time samples exist.
        """
        times = attr.GetTimeSamples()
        if not times:
            return []

        # Resolve conversion factor once
        dim = get_dimension(attr.GetName())
        if dim is None:
            from .per_attribute import PerAttributeUnits
            annotation = PerAttributeUnits.get_annotation(attr)
            if annotation is not None:
                from .per_attribute import str_to_dimension
                dim = str_to_dimension(annotation["dimension"])
                source_mpu = annotation["metersPerUnit"]
                source_kpu = annotation["kilogramsPerUnit"]
                factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)
            else:
                return [(t, attr.Get(t)) for t in times]
        elif dim == _ZERO_DIM:
            return [(t, attr.Get(t)) for t in times]
        else:
            prim = attr.GetPrim()
            metrics = _metrics_cache.get(prim)
            source_mpu = metrics["metersPerUnit"]
            source_kpu = metrics["kilogramsPerUnit"]
            factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)

        return [(t, _apply_factor(attr.Get(t), factor)) for t in times]

    @staticmethod
    def set_time_samples(attr: Usd.Attribute, samples: list[tuple[float, object]],
                         source_mpu: float = 1.0, source_kpu: float = 1.0) -> bool:
        """Set multiple time samples, converting each from source units.

        samples: list of (time, value) tuples in source units.
        Resolves the conversion factor once and applies it to every sample.
        """
        if not samples:
            return True

        dim = get_dimension(attr.GetName())
        if dim is None or dim == _ZERO_DIM:
            for t, v in samples:
                attr.Set(v, t)
            return True

        prim = attr.GetPrim()
        metrics = _metrics_cache.get(prim)
        target_mpu = metrics["metersPerUnit"]
        target_kpu = metrics["kilogramsPerUnit"]
        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)

        for t, v in samples:
            attr.Set(_apply_factor(v, factor), t)
        return True

    # ------------------------------------------------------------------
    # Cache management
    # ------------------------------------------------------------------

    @staticmethod
    def clear_cache():
        """Clear the internal metrics cache.
        
        Call this after editing MetricsAPI on any prim, or when switching stages.
        """
        _metrics_cache.clear()

    # ------------------------------------------------------------------
    # Spline (animation curve) support
    # ------------------------------------------------------------------

    @staticmethod
    def get_spline(attr: Usd.Attribute, target_mpu: float = 1.0,
                   target_kpu: float = 1.0):
        """Get an attribute's spline (animation curve) with values converted to target units.

        Scales knot values and tangent slopes by the conversion factor.
        Tangent widths (time) are preserved.
        Returns None if the attribute has no spline.
        
        For linear attributes (L=1): values and slopes multiply by mpu_ratio.
        For derived quantities (e.g. density L⁻³·M¹): same dimensional exponent logic.
        """
        if not attr.HasSpline():
            return None

        spline = attr.GetSpline()
        if spline.IsEmpty():
            return None

        dim = get_dimension(attr.GetName())
        if dim is None or dim == _ZERO_DIM:
            return spline  # unitless or unknown — return as-is

        prim = attr.GetPrim()
        metrics = _metrics_cache.get(prim)
        source_mpu = metrics["metersPerUnit"]
        source_kpu = metrics["kilogramsPerUnit"]
        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)

        if factor == 1.0:
            return spline

        return _scale_spline(spline, factor)

    @staticmethod
    def set_spline(attr: Usd.Attribute, spline, source_mpu: float = 1.0,
                   source_kpu: float = 1.0):
        """Set an attribute's spline, converting from source units to prim's native units.

        The inverse of get_spline().
        """
        dim = get_dimension(attr.GetName())
        if dim is None or dim == _ZERO_DIM:
            attr.SetSpline(spline)
            return

        prim = attr.GetPrim()
        metrics = _metrics_cache.get(prim)
        target_mpu = metrics["metersPerUnit"]
        target_kpu = metrics["kilogramsPerUnit"]
        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)

        if factor == 1.0:
            attr.SetSpline(spline)
            return

        scaled = _scale_spline(spline, factor)
        attr.SetSpline(scaled)

    @staticmethod
    def get_conversion_info(attr: Usd.Attribute) -> dict:
        """Debug helper: return dict with source_mpu, dimension, conversion_factor, etc."""
        prim = attr.GetPrim()
        metrics = MetricsAPI.get_effective_metrics(prim)
        source_mpu = metrics["metersPerUnit"]
        source_kpu = metrics["kilogramsPerUnit"]
        dim = get_dimension(attr.GetName())
        unit_source = "registry"
        if dim is None:
            from .per_attribute import PerAttributeUnits, str_to_dimension
            annotation = PerAttributeUnits.get_annotation(attr)
            if annotation is not None:
                dim = str_to_dimension(annotation["dimension"])
                source_mpu = annotation["metersPerUnit"]
                source_kpu = annotation["kilogramsPerUnit"]
                unit_source = "per_attribute"
            else:
                unit_source = "passthrough"
        factor_to_m = (
            conversion_factor(source_mpu, 1.0, dim, source_kpu, 1.0)
            if (dim is not None and dim != _ZERO_DIM)
            else 1.0
        )
        return {
            "attr_name": attr.GetName(),
            "source_mpu": source_mpu,
            "source_kpu": source_kpu,
            "dimension": dim,
            "conversion_factor_to_meters": factor_to_m,
            "unit_source": unit_source,
        }

    @staticmethod
    def set_translate(prim: Usd.Prim, value: Gf.Vec3d, source_mpu: float = 1.0,
                      time=None) -> bool:
        """Set a prim's translation in source units, converting to prim's native units.

        Creates or reuses xformOp:translate on the prim.

        Example: set_translate(bolt_prim, Gf.Vec3d(0.01, 0, 0), source_mpu=1.0)
        If bolt is in mm (mpu=0.001): stores (10, 0, 0)
        """
        if time is None:
            time = Usd.TimeCode.Default()
        metrics = MetricsAPI.get_effective_metrics(prim)
        prim_mpu = metrics["metersPerUnit"]
        factor = source_mpu / prim_mpu
        converted = value * factor if factor != 1.0 else value
        xformable = UsdGeom.Xformable(prim)
        ops = xformable.GetOrderedXformOps()
        existing = next((op for op in ops if op.GetOpName() == "xformOp:translate"), None)
        op = existing if existing is not None else xformable.AddTranslateOp()
        return op.Set(converted, time)

    @staticmethod
    def get_translate(prim: Usd.Prim, target_mpu: float = 1.0,
                      time=None) -> Gf.Vec3d | None:
        """Get a prim's translation converted to target units.

        Reads xformOp:translate and converts from prim's native units.
        Returns None if no translate op exists.
        """
        if time is None:
            time = Usd.TimeCode.Default()
        xformable = UsdGeom.Xformable(prim)
        ops = xformable.GetOrderedXformOps()
        translate_ops = [op for op in ops if op.GetOpName() == "xformOp:translate"]
        if not translate_ops:
            return None
        val = translate_ops[0].Get(time)
        if val is None:
            return None
        metrics = MetricsAPI.get_effective_metrics(prim)
        prim_mpu = metrics["metersPerUnit"]
        factor = prim_mpu / target_mpu
        return val * factor if factor != 1.0 else val

    @staticmethod
    def set_scale(prim: Usd.Prim, value: Gf.Vec3d, time=None) -> bool:
        """Set a prim's scale. Scale is unitless (ratio), so no conversion needed.
        Included for API completeness."""
        if time is None:
            time = Usd.TimeCode.Default()
        xformable = UsdGeom.Xformable(prim)
        ops = xformable.GetOrderedXformOps()
        existing = next((op for op in ops if op.GetOpName() == "xformOp:scale"), None)
        op = existing if existing is not None else xformable.AddScaleOp()
        return op.Set(value, time)

    @staticmethod
    def get_local_transform(prim: Usd.Prim, target_mpu: float = 1.0,
                            time=None) -> Gf.Matrix4d:
        """Get the prim's full local transform matrix, with translation converted to target units.
        Uses UsdGeom.XformCache for proper evaluation."""
        if time is None:
            time = Usd.TimeCode.Default()
        cache = UsdGeom.XformCache(time)
        world_xf = cache.GetLocalToWorldTransform(prim)
        parent = prim.GetParent()
        if parent.IsValid():
            parent_world = cache.GetLocalToWorldTransform(parent)
            local_xf = parent_world.GetInverse() * world_xf
        else:
            local_xf = world_xf
        metrics = MetricsAPI.get_effective_metrics(prim)
        source_mpu = metrics["metersPerUnit"]
        factor = source_mpu / target_mpu
        if factor != 1.0:
            t = local_xf.ExtractTranslation()
            row = local_xf.GetRow(3)
            local_xf.SetRow(3, Gf.Vec4d(t[0] * factor, t[1] * factor, t[2] * factor, row[3]))
        return local_xf

    @staticmethod
    def get_world_position(prim: Usd.Prim, target_mpu: float = 1.0,
                           time=None) -> Gf.Vec3d:
        """Get the prim's world-space position in target units.

        This is the "give me this prim's position in meters" convenience method.
        Accounts for the full transform stack including any assembly corrections.

        Uses UsdGeom.XformCache to get local-to-world, extracts translation,
        then converts from stage root's effective mpu to target_mpu.
        """
        if time is None:
            time = Usd.TimeCode.Default()
        cache = UsdGeom.XformCache(time)
        world_xf = cache.GetLocalToWorldTransform(prim)
        world_pos = world_xf.ExtractTranslation()
        root_mpu = _find_root_mpu(prim)
        factor = root_mpu / target_mpu
        return world_pos * factor if factor != 1.0 else world_pos

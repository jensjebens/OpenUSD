"""Per-attribute unit metadata — alternative to MetricsAPI + registry.

Each attribute carries its own unit annotation in customData:
  customData = {
      units = {
          dimension = "L1"           # dimensional exponents string
          metersPerUnit = 0.01       # what unit system this value is in
          kilogramsPerUnit = 1.0     # (optional, for mass-bearing attrs)
      }
  }

This is self-describing: no external registry needed.
"""

from pxr import Usd, Gf, Vt

from .dimensions import Dimension, DIMENSION_REGISTRY, conversion_factor
from .units_lens import _apply_factor

_UNITS_KEY = "units"
_ZERO_DIM = Dimension(0, 0, 0)


# ---------------------------------------------------------------------------
# Dimension string encoding
# ---------------------------------------------------------------------------

def dimension_to_str(dim: Dimension) -> str:
    """Dimension(1,0,0) → 'L1', Dimension(-3,1,0) → 'L-3_M1', Dimension(0,0,0) → ''"""
    parts = []
    if dim.L != 0:
        parts.append(f"L{dim.L}")
    if dim.M != 0:
        parts.append(f"M{dim.M}")
    if dim.T != 0:
        parts.append(f"T{dim.T}")
    return "_".join(parts)


def str_to_dimension(s: str) -> Dimension:
    """'L-3_M1' → Dimension(-3,1,0), '' → Dimension(0,0,0)"""
    if not s:
        return Dimension(0, 0, 0)
    L, M, T = 0, 0, 0
    for part in s.split("_"):
        if part.startswith("L"):
            L = int(part[1:])
        elif part.startswith("M"):
            M = int(part[1:])
        elif part.startswith("T"):
            T = int(part[1:])
    return Dimension(L=L, M=M, T=T)


# ---------------------------------------------------------------------------
# PerAttributeUnits
# ---------------------------------------------------------------------------

class PerAttributeUnits:
    """Per-attribute unit metadata approach."""

    @staticmethod
    def annotate(attr: Usd.Attribute, dimension: Dimension,
                 meters_per_unit: float = 1.0, kilograms_per_unit: float = 1.0):
        """Annotate an attribute with its dimensional exponents and unit context.

        Stores in customData["units"] on the attribute.
        """
        data = {
            "dimension": dimension_to_str(dimension),
            "metersPerUnit": meters_per_unit,
            "kilogramsPerUnit": kilograms_per_unit,
        }
        attr.SetCustomDataByKey(_UNITS_KEY, data)

    @staticmethod
    def get_annotation(attr: Usd.Attribute) -> dict | None:
        """Read the unit annotation from an attribute.

        Returns dict with 'dimension', 'metersPerUnit', 'kilogramsPerUnit'
        or None if not annotated.
        """
        data = attr.GetCustomDataByKey(_UNITS_KEY)
        if not data:
            return None
        return {
            "dimension": data.get("dimension", ""),
            "metersPerUnit": float(data.get("metersPerUnit", 1.0)),
            "kilogramsPerUnit": float(data.get("kilogramsPerUnit", 1.0)),
        }

    @staticmethod
    def has_annotation(attr: Usd.Attribute) -> bool:
        """Check if attribute has unit annotation."""
        return bool(attr.GetCustomDataByKey(_UNITS_KEY))

    @staticmethod
    def get_attr(attr: Usd.Attribute, target_mpu: float = 1.0, target_kpu: float = 1.0,
                 time=None):
        """Get attribute value converted to target units using per-attribute metadata.

        Unlike UnitsLens.get_attr which uses MetricsAPI + registry,
        this reads the unit info directly from the attribute's customData.

        Returns raw value if attribute has no annotation.
        """
        if time is None:
            time = Usd.TimeCode.Default()

        val = attr.Get(time)
        if val is None:
            return None

        annotation = PerAttributeUnits.get_annotation(attr)
        if annotation is None:
            return val

        dim = str_to_dimension(annotation["dimension"])
        if dim == _ZERO_DIM:
            return val

        source_mpu = annotation["metersPerUnit"]
        source_kpu = annotation["kilogramsPerUnit"]
        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)
        return _apply_factor(val, factor)

    @staticmethod
    def set_attr(attr: Usd.Attribute, value, source_mpu: float = 1.0, source_kpu: float = 1.0,
                 time=None) -> bool:
        """Set attribute value, converting from source units to the attribute's annotated units.

        ALSO updates the annotation's metersPerUnit to match the stored value's units
        (which is the attribute's existing annotation, not the source units).
        """
        if time is None:
            time = Usd.TimeCode.Default()

        annotation = PerAttributeUnits.get_annotation(attr)
        if annotation is None:
            return attr.Set(value, time)

        dim = str_to_dimension(annotation["dimension"])
        if dim == _ZERO_DIM:
            return attr.Set(value, time)

        target_mpu = annotation["metersPerUnit"]
        target_kpu = annotation["kilogramsPerUnit"]
        factor = conversion_factor(source_mpu, target_mpu, dim, source_kpu, target_kpu)
        converted = _apply_factor(value, factor)
        return attr.Set(converted, time)

    @staticmethod
    def annotate_prim(prim: Usd.Prim, meters_per_unit: float,
                      kilograms_per_unit: float = 1.0,
                      registry: dict = None):
        """Convenience: annotate ALL attributes on a prim using a dimension registry.

        For each attribute, look up its dimension in the provided registry
        (or the default DIMENSION_REGISTRY), and annotate it with that dimension
        plus the given metersPerUnit.

        This shows the authoring burden: you still need a registry to bootstrap,
        AND you're writing metadata to every attribute.
        """
        if registry is None:
            registry = DIMENSION_REGISTRY
        for attr in prim.GetAttributes():
            dim = registry.get(attr.GetName(), _ZERO_DIM)
            PerAttributeUnits.annotate(attr, dim, meters_per_unit, kilograms_per_unit)

    @staticmethod
    def annotate_stage(stage: Usd.Stage, meters_per_unit: float,
                       kilograms_per_unit: float = 1.0,
                       registry: dict = None):
        """Annotate ALL attributes on ALL prims in a stage.

        Demonstrates the storage/authoring overhead of this approach.
        """
        for prim in stage.Traverse():
            PerAttributeUnits.annotate_prim(prim, meters_per_unit, kilograms_per_unit, registry)

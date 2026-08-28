"""Tests for the per-attribute unit metadata approach."""

import math
import pytest
from pxr import Usd, UsdGeom, Sdf, Gf, Vt

from units_api import (
    Dimension, DIMENSION_REGISTRY, MetricsAPI, UnitsLens,
    PerAttributeUnits, dimension_to_str, str_to_dimension,
)
from test_stages import (
    build_stage2_physics,
    build_stage4_deep_nesting,
    build_stage5_custom_attrs,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_stage_with_float_attr(value: float, attr_name: str = "myAttr") -> tuple:
    """Return (stage, prim, attr) with a single float attribute set to value."""
    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Test", "Xform")
    attr = prim.CreateAttribute(attr_name, Sdf.ValueTypeNames.Float)
    attr.Set(value)
    return stage, prim, attr


# ---------------------------------------------------------------------------
# Annotation
# ---------------------------------------------------------------------------

def test_annotate_and_read_back():
    """Annotate a translate attr with L1, read annotation back."""
    stage = Usd.Stage.CreateInMemory()
    xform = UsdGeom.Xform.Define(stage, "/Xform")
    attr = xform.GetPrim().CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)

    PerAttributeUnits.annotate(attr, Dimension(L=1), meters_per_unit=0.01)

    ann = PerAttributeUnits.get_annotation(attr)
    assert ann is not None
    assert ann["dimension"] == "L1"
    assert ann["metersPerUnit"] == pytest.approx(0.01)
    assert ann["kilogramsPerUnit"] == pytest.approx(1.0)


def test_annotate_density():
    """Annotate density with L-3_M1."""
    stage, prim, attr = _make_stage_with_float_attr(2700.0, "physics:density")

    PerAttributeUnits.annotate(attr, Dimension(L=-3, M=1), meters_per_unit=1.0,
                               kilograms_per_unit=1.0)

    ann = PerAttributeUnits.get_annotation(attr)
    assert ann is not None
    assert ann["dimension"] == "L-3_M1"
    assert ann["metersPerUnit"] == pytest.approx(1.0)
    assert ann["kilogramsPerUnit"] == pytest.approx(1.0)


def test_has_annotation():
    """Annotated → True, unannotated → False."""
    stage, prim, attr = _make_stage_with_float_attr(1.0)
    assert not PerAttributeUnits.has_annotation(attr)

    PerAttributeUnits.annotate(attr, Dimension(L=1), meters_per_unit=1.0)
    assert PerAttributeUnits.has_annotation(attr)


# ---------------------------------------------------------------------------
# Dimension string encoding
# ---------------------------------------------------------------------------

def test_dimension_to_str_length():
    """Dimension(1,0,0) → 'L1'"""
    assert dimension_to_str(Dimension(1, 0, 0)) == "L1"


def test_dimension_to_str_density():
    """Dimension(-3,1,0) → 'L-3_M1'"""
    assert dimension_to_str(Dimension(-3, 1, 0)) == "L-3_M1"


def test_dimension_to_str_unitless():
    """Dimension(0,0,0) → ''"""
    assert dimension_to_str(Dimension(0, 0, 0)) == ""


def test_str_to_dimension_roundtrip():
    """Encode then decode → same Dimension."""
    dims = [
        Dimension(1, 0, 0),
        Dimension(-3, 1, 0),
        Dimension(1, 0, -2),
        Dimension(0, 0, 0),
        Dimension(1, -1, 0),
        Dimension(3, 0, -1),
    ]
    for dim in dims:
        s = dimension_to_str(dim)
        assert str_to_dimension(s) == dim, f"roundtrip failed for {dim}: got '{s}'"


# ---------------------------------------------------------------------------
# Get/Set with per-attribute metadata
# ---------------------------------------------------------------------------

def test_get_attr_length_cm_to_m():
    """Attribute annotated as L1 with mpu=0.01 (cm). Get in meters."""
    stage, prim, attr = _make_stage_with_float_attr(100.0)  # 100 cm
    PerAttributeUnits.annotate(attr, Dimension(L=1), meters_per_unit=0.01)

    result = PerAttributeUnits.get_attr(attr, target_mpu=1.0)
    assert result == pytest.approx(1.0)  # 100 cm → 1 m


def test_get_attr_density():
    """Attribute annotated as L-3_M1 with mpu=1.0 (m). Get in cm (mpu=0.01)."""
    stage, prim, attr = _make_stage_with_float_attr(1000.0)  # 1000 kg/m³
    PerAttributeUnits.annotate(attr, Dimension(L=-3, M=1),
                               meters_per_unit=1.0, kilograms_per_unit=1.0)

    # Convert to cm context: target_mpu=0.01, target_kpu=1.0
    # factor = (1.0/0.01)^-3 * (1.0/1.0)^1 = 100^-3 = 1e-6
    result = PerAttributeUnits.get_attr(attr, target_mpu=0.01, target_kpu=1.0)
    assert result == pytest.approx(1000.0 * 1e-6)


def test_get_attr_unannotated_passthrough():
    """Unannotated attribute → raw value returned."""
    stage, prim, attr = _make_stage_with_float_attr(42.0)

    result = PerAttributeUnits.get_attr(attr, target_mpu=1.0)
    assert result == pytest.approx(42.0)


def test_set_attr_m_to_cm():
    """Set a value in meters on a cm-annotated attribute."""
    stage, prim, attr = _make_stage_with_float_attr(0.0)
    PerAttributeUnits.annotate(attr, Dimension(L=1), meters_per_unit=0.01)

    # source is 5 m, attribute is in cm → should store 500
    PerAttributeUnits.set_attr(attr, 5.0, source_mpu=1.0)
    raw = attr.Get()
    assert raw == pytest.approx(500.0)


def test_set_get_roundtrip():
    """Set in meters, get in meters → same value."""
    stage, prim, attr = _make_stage_with_float_attr(0.0)
    PerAttributeUnits.annotate(attr, Dimension(L=1), meters_per_unit=0.01)

    PerAttributeUnits.set_attr(attr, 3.14, source_mpu=1.0)
    result = PerAttributeUnits.get_attr(attr, target_mpu=1.0)
    assert result == pytest.approx(3.14)


# ---------------------------------------------------------------------------
# Custom attributes (the killer use case)
# ---------------------------------------------------------------------------

def test_custom_attr_with_annotation():
    """myPipeline:flowRate annotated as L3_T-1 → CAN be converted.

    This is where per-attribute wins: custom attrs work without registry.
    """
    stage = build_stage5_custom_attrs()
    seg_prim = stage.GetPrimAtPath("/Pipe/Segment")
    flow_attr = seg_prim.GetAttribute("myPipeline:flowRate")
    assert flow_attr.IsValid()

    # Annotate as volumetric flow (L³/T = L3_T-1), authored in meters
    PerAttributeUnits.annotate(flow_attr, Dimension(L=3, T=-1), meters_per_unit=1.0)

    # Get in cm context (mpu=0.01): factor = (1.0/0.01)^3 = 1e6
    raw = flow_attr.Get()  # 0.002 m³/s
    result = PerAttributeUnits.get_attr(flow_attr, target_mpu=0.01)
    assert result == pytest.approx(raw * 1e6)


def test_custom_attr_without_annotation():
    """Same attr without annotation → passthrough (same as MetricsAPI approach)."""
    stage = build_stage5_custom_attrs()
    seg_prim = stage.GetPrimAtPath("/Pipe/Segment")
    flow_attr = seg_prim.GetAttribute("myPipeline:flowRate")
    assert flow_attr.IsValid()

    raw = flow_attr.Get()
    result = PerAttributeUnits.get_attr(flow_attr, target_mpu=0.01)
    assert result == pytest.approx(raw)  # passthrough — no annotation


# ---------------------------------------------------------------------------
# Bulk annotation
# ---------------------------------------------------------------------------

def test_annotate_prim():
    """Annotate all attrs on a prim. Check that known attrs get dimensions
    and unknown/unitless attrs get empty dimension."""
    stage = build_stage2_physics()
    arm = stage.GetPrimAtPath("/World/Robot/Arm")
    PerAttributeUnits.annotate_prim(arm, meters_per_unit=1.0, kilograms_per_unit=1.0)

    vel_attr = arm.GetAttribute("physics:velocity")
    mass_attr = arm.GetAttribute("physics:mass")

    vel_ann = PerAttributeUnits.get_annotation(vel_attr)
    mass_ann = PerAttributeUnits.get_annotation(mass_attr)

    assert vel_ann is not None
    assert vel_ann["dimension"] == "L1_T-1"  # Dimension(L=1, T=-1)

    assert mass_ann is not None
    assert mass_ann["dimension"] == "M1"


def test_annotate_stage_overhead():
    """Annotate entire stage. Count how many customData entries were written.
    Compare with MetricsAPI approach (which uses 1-2 annotations total).
    This demonstrates the storage overhead."""
    stage = build_stage2_physics()

    # Count MetricsAPI annotations (prim-level)
    metrics_count = sum(
        1 for prim in stage.Traverse()
        if MetricsAPI.has_metrics(prim)
    )

    # Annotate all attributes
    PerAttributeUnits.annotate_stage(stage, meters_per_unit=1.0, kilograms_per_unit=1.0)

    # Count per-attribute annotations
    per_attr_count = sum(
        1
        for prim in stage.Traverse()
        for attr in prim.GetAttributes()
        if PerAttributeUnits.has_annotation(attr)
    )

    # MetricsAPI has very few prim-level annotations
    assert metrics_count <= 3
    # Per-attribute has far more (every attribute on every prim)
    assert per_attr_count > metrics_count
    # Sanity: at least one attribute was annotated
    assert per_attr_count > 0


# ---------------------------------------------------------------------------
# Head-to-head comparison
# ---------------------------------------------------------------------------

def test_compare_approaches_stage2():
    """Run the same conversion on Stage 2 (physics) using both approaches:
    1. MetricsAPI + registry (UnitsLens.get_attr)
    2. Per-attribute (PerAttributeUnits.get_attr)
    Results should be identical.
    """
    stage = build_stage2_physics()
    arm = stage.GetPrimAtPath("/World/Robot/Arm")
    vel_attr = arm.GetAttribute("physics:velocity")

    # Approach 1: MetricsAPI + registry
    result_a = UnitsLens.get_attr(vel_attr, target_mpu=1.0)

    # Approach 2: per-attribute — annotate first, then get
    ann_stage = build_stage2_physics()
    ann_arm = ann_stage.GetPrimAtPath("/World/Robot/Arm")
    ann_vel = ann_arm.GetAttribute("physics:velocity")
    # Robot is at mpu=1.0 (meters)
    PerAttributeUnits.annotate(ann_vel, Dimension(L=1, T=-1),
                               meters_per_unit=1.0, kilograms_per_unit=1.0)
    result_b = PerAttributeUnits.get_attr(ann_vel, target_mpu=1.0)

    assert result_a[0] == pytest.approx(result_b[0])
    assert result_a[1] == pytest.approx(result_b[1])
    assert result_a[2] == pytest.approx(result_b[2])


def test_compare_custom_attr_coverage():
    """Using Stage 5 (custom attrs):
    - MetricsAPI: custom attrs pass through (no registry entry)
    - Per-attribute: custom attrs with annotation CAN be converted
    This is the key advantage of per-attribute.
    """
    stage_a = build_stage5_custom_attrs()
    seg_a = stage_a.GetPrimAtPath("/Pipe/Segment")
    flow_a = seg_a.GetAttribute("myPipeline:flowRate")

    # MetricsAPI + registry: no registry entry → passthrough
    result_metrics = UnitsLens.get_attr(flow_a, target_mpu=0.01)
    raw_value = flow_a.Get()
    # passthrough: no registry entry for custom attr
    assert result_metrics == pytest.approx(raw_value)

    # Per-attribute: annotate then convert
    stage_b = build_stage5_custom_attrs()
    seg_b = stage_b.GetPrimAtPath("/Pipe/Segment")
    flow_b = seg_b.GetAttribute("myPipeline:flowRate")
    PerAttributeUnits.annotate(flow_b, Dimension(L=3, T=-1), meters_per_unit=1.0)
    result_per_attr = PerAttributeUnits.get_attr(flow_b, target_mpu=0.01)

    # Per-attribute result IS different from raw (conversion applied)
    assert result_per_attr != pytest.approx(raw_value)
    # And it equals raw × (1.0/0.01)^3 = raw × 1e6
    assert result_per_attr == pytest.approx(raw_value * 1e6)


def test_compare_authoring_burden():
    """Count annotations needed for each approach on Stage 4 (deep nesting):
    - MetricsAPI: 2 prims annotated (Factory + Machine)
    - Per-attribute: count every attribute annotated
    Report the ratio.
    """
    stage = build_stage4_deep_nesting()

    # MetricsAPI annotations
    metrics_count = sum(
        1 for prim in stage.Traverse()
        if MetricsAPI.has_metrics(prim)
    )
    assert metrics_count == 2  # Factory + Machine

    # Per-attribute annotations — annotate_stage writes to every attr
    PerAttributeUnits.annotate_stage(stage, meters_per_unit=1.0, kilograms_per_unit=1.0)
    per_attr_count = sum(
        1
        for prim in stage.Traverse()
        for attr in prim.GetAttributes()
        if PerAttributeUnits.has_annotation(attr)
    )

    # Ratio: per-attribute authoring burden vs MetricsAPI
    ratio = per_attr_count / metrics_count
    # Per-attribute always requires far more annotations
    assert ratio > 1

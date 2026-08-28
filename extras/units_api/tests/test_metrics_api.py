"""Tests for MetricsAPI — prim-level unit declarations with ancestor inheritance."""

import pytest
from pxr import Usd, UsdGeom
from units_api import MetricsAPI
from test_stages import build_stage1_bolt_in_factory, build_stage4_deep_nesting


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _stage_with_prim(path="/Root") -> tuple[Usd.Stage, Usd.Prim]:
    stage = Usd.Stage.CreateInMemory()
    prim = UsdGeom.Xform.Define(stage, path).GetPrim()
    return stage, prim


# ---------------------------------------------------------------------------
# Basic apply / query
# ---------------------------------------------------------------------------

def test_apply_and_query():
    """Apply MetricsAPI to a prim and read it back."""
    _, prim = _stage_with_prim()
    assert not MetricsAPI.has_metrics(prim)

    MetricsAPI.apply(prim, meters_per_unit=0.01, up_axis="Y", kilograms_per_unit=1.0)

    assert MetricsAPI.has_metrics(prim)
    m = MetricsAPI.get_metrics(prim)
    assert m["metersPerUnit"] == pytest.approx(0.01)
    assert m["upAxis"] == "Y"
    assert m["kilogramsPerUnit"] == pytest.approx(1.0)


def test_apply_partial():
    """Applying only some fields should not clobber others."""
    _, prim = _stage_with_prim()
    MetricsAPI.apply(prim, meters_per_unit=1.0)
    MetricsAPI.apply(prim, up_axis="Z")

    m = MetricsAPI.get_metrics(prim)
    assert m["metersPerUnit"] == pytest.approx(1.0)
    assert m["upAxis"] == "Z"


# ---------------------------------------------------------------------------
# Ancestor inheritance
# ---------------------------------------------------------------------------

def test_ancestor_inheritance():
    """Child prim inherits MetricsAPI from its parent."""
    stage = Usd.Stage.CreateInMemory()
    parent = UsdGeom.Xform.Define(stage, "/Parent").GetPrim()
    child = UsdGeom.Xform.Define(stage, "/Parent/Child").GetPrim()

    MetricsAPI.apply(parent, meters_per_unit=1.0, up_axis="Y", kilograms_per_unit=1.0)

    assert not MetricsAPI.has_metrics(child)
    m = MetricsAPI.get_effective_metrics(child)
    assert m["metersPerUnit"] == pytest.approx(1.0)
    assert m["upAxis"] == "Y"


def test_grandchild_inherits_from_grandparent():
    """Grandchild inherits through intermediate prim with no MetricsAPI."""
    stage = Usd.Stage.CreateInMemory()
    gp = UsdGeom.Xform.Define(stage, "/GP").GetPrim()
    UsdGeom.Xform.Define(stage, "/GP/Parent")
    gc = UsdGeom.Xform.Define(stage, "/GP/Parent/GC").GetPrim()

    MetricsAPI.apply(gp, meters_per_unit=0.001, up_axis="Z", kilograms_per_unit=1.0)

    m = MetricsAPI.get_effective_metrics(gc)
    assert m["metersPerUnit"] == pytest.approx(0.001)
    assert m["upAxis"] == "Z"


def test_override():
    """Child MetricsAPI overrides parent's."""
    stage = Usd.Stage.CreateInMemory()
    parent = UsdGeom.Xform.Define(stage, "/Parent").GetPrim()
    child = UsdGeom.Xform.Define(stage, "/Parent/Child").GetPrim()

    MetricsAPI.apply(parent, meters_per_unit=1.0, up_axis="Y", kilograms_per_unit=1.0)
    MetricsAPI.apply(child, meters_per_unit=0.001)

    m = MetricsAPI.get_effective_metrics(child)
    assert m["metersPerUnit"] == pytest.approx(0.001)


# ---------------------------------------------------------------------------
# Fallback: stage-level layer metadata
# ---------------------------------------------------------------------------

def test_layer_metadata_fallback():
    """No MetricsAPI on any prim → falls back to stage metadata."""
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageMetersPerUnit(stage, 0.01)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
    assert not MetricsAPI.has_metrics(prim)

    m = MetricsAPI.get_effective_metrics(prim)
    assert m["metersPerUnit"] == pytest.approx(0.01)
    assert m["upAxis"] == "Y"


def test_default_fallback():
    """No MetricsAPI, no stage metadata → USD defaults (cm, Y-up, 1kg)."""
    stage = Usd.Stage.CreateInMemory()
    # Don't set any stage metadata
    prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()

    m = MetricsAPI.get_effective_metrics(prim)
    # USD default is 0.01 (cm) when nothing is set — GetStageMetersPerUnit returns 0.01
    # If it returns 0 or negative, our code uses 0.01 fallback.
    assert m["metersPerUnit"] > 0
    assert m["upAxis"] in ("Y", "Z")
    assert m["kilogramsPerUnit"] > 0


# ---------------------------------------------------------------------------
# Stage 1: bolt-in-factory
# ---------------------------------------------------------------------------

def test_stage1_bolt_in_factory():
    """mm bolt referenced into m factory — MetricsAPI boundary test."""
    stage = build_stage1_bolt_in_factory()

    factory = stage.GetPrimAtPath("/Factory")
    bolt = stage.GetPrimAtPath("/Factory/Equipment/Bolt")
    shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")

    assert MetricsAPI.get_meters_per_unit(factory) == pytest.approx(1.0)
    assert MetricsAPI.get_meters_per_unit(bolt) == pytest.approx(0.001)
    # Shaft inherits from bolt
    assert MetricsAPI.get_meters_per_unit(shaft) == pytest.approx(0.001)

    floor = stage.GetPrimAtPath("/Factory/Floor")
    assert MetricsAPI.get_meters_per_unit(floor) == pytest.approx(1.0)


# ---------------------------------------------------------------------------
# Stage 4: deep nesting
# ---------------------------------------------------------------------------

def test_stage4_deep_nesting():
    """6 levels deep, 2 MetricsAPI boundaries."""
    stage = build_stage4_deep_nesting()

    factory = stage.GetPrimAtPath("/Factory")
    hall = stage.GetPrimAtPath("/Factory/Building/Hall")
    machine = stage.GetPrimAtPath("/Factory/Building/CNC_Area/Machine")
    bit = stage.GetPrimAtPath("/Factory/Building/CNC_Area/Machine/Spindle/Tool/Bit")

    assert MetricsAPI.get_meters_per_unit(factory) == pytest.approx(1.0)
    assert MetricsAPI.get_meters_per_unit(hall) == pytest.approx(1.0)
    assert MetricsAPI.get_meters_per_unit(machine) == pytest.approx(0.001)
    assert MetricsAPI.get_meters_per_unit(bit) == pytest.approx(0.001)


# ---------------------------------------------------------------------------
# Convenience method
# ---------------------------------------------------------------------------

def test_get_meters_per_unit():
    """Convenience method returns the same value as get_effective_metrics."""
    _, prim = _stage_with_prim()
    MetricsAPI.apply(prim, meters_per_unit=0.1)
    assert MetricsAPI.get_meters_per_unit(prim) == pytest.approx(0.1)
    assert MetricsAPI.get_meters_per_unit(prim) == pytest.approx(
        MetricsAPI.get_effective_metrics(prim)["metersPerUnit"]
    )

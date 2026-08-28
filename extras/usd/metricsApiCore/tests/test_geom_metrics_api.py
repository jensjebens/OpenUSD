"""Symmetric tests for GeomMetricsAPI / PhysicsMetricsAPI (C++ bindings).

These tests mirror extras/units_api/tests/test_metrics_api.py exactly.
Same test names, same assertions, different implementation under test.

When run against the C++ core (pxr.UsdMetricsApi), they validate the
schema-based implementation. When the Python POC passes the same tests,
the two implementations are proven equivalent.

Requires: USD built with metricsApiCore (extras/usd/metricsApiCore).
"""

import pytest
from pxr import Usd, UsdGeom

# Import from C++ bindings — skip entire module if not built
UsdMetricsApi = pytest.importorskip("pxr.UsdMetricsApi")


# ---------------------------------------------------------------------------
# Helpers — mirror the Python POC's MetricsAPI interface using C++ bindings
# ---------------------------------------------------------------------------

def _apply(prim, meters_per_unit=None, up_axis=None, kilograms_per_unit=None):
    """Apply GeomMetricsAPI and/or PhysicsMetricsAPI to a prim."""
    if meters_per_unit is not None or up_axis is not None:
        api = UsdMetricsApi.GeomMetricsAPI.Apply(prim)
        if meters_per_unit is not None:
            api.CreateMetersPerUnitAttr().Set(meters_per_unit)
        if up_axis is not None:
            api.CreateUpAxisAttr().Set(up_axis)
    if kilograms_per_unit is not None:
        api = UsdMetricsApi.PhysicsMetricsAPI.Apply(prim)
        api.CreateKilogramsPerUnitAttr().Set(kilograms_per_unit)


def _has_metrics(prim):
    """Check if GeomMetricsAPI is applied."""
    return UsdMetricsApi.GeomMetricsAPI(prim).GetMetersPerUnitAttr().HasAuthoredValue()


def _stage_with_prim(path="/Root"):
    stage = Usd.Stage.CreateInMemory()
    prim = UsdGeom.Xform.Define(stage, path).GetPrim()
    return stage, prim


# ---------------------------------------------------------------------------
# Basic apply / query — mirrors test_apply_and_query
# ---------------------------------------------------------------------------

def test_apply_and_query():
    """Apply metrics to a prim and read back via schema attributes."""
    _, prim = _stage_with_prim()
    _apply(prim, meters_per_unit=0.01, up_axis="Y", kilograms_per_unit=1.0)

    api = UsdMetricsApi.GeomMetricsAPI(prim)
    assert api.GetMetersPerUnitAttr().Get() == pytest.approx(0.01)
    assert api.GetUpAxisAttr().Get() == "Y"

    phys = UsdMetricsApi.PhysicsMetricsAPI(prim)
    assert phys.GetKilogramsPerUnitAttr().Get() == pytest.approx(1.0)


def test_apply_partial():
    """Applying only some fields should not clobber others."""
    _, prim = _stage_with_prim()
    _apply(prim, meters_per_unit=1.0)
    _apply(prim, up_axis="Z")

    api = UsdMetricsApi.GeomMetricsAPI(prim)
    assert api.GetMetersPerUnitAttr().Get() == pytest.approx(1.0)
    assert api.GetUpAxisAttr().Get() == "Z"


# ---------------------------------------------------------------------------
# Ancestor inheritance — mirrors test_ancestor_inheritance etc.
# ---------------------------------------------------------------------------

def test_ancestor_inheritance():
    """Child prim inherits metrics from parent via GetEffectiveMetersPerUnit."""
    stage = Usd.Stage.CreateInMemory()
    parent = UsdGeom.Xform.Define(stage, "/Parent").GetPrim()
    child = UsdGeom.Xform.Define(stage, "/Parent/Child").GetPrim()

    _apply(parent, meters_per_unit=1.0, up_axis="Y", kilograms_per_unit=1.0)

    mpu = UsdMetricsApi.GetEffectiveMetersPerUnit(child)
    assert mpu == pytest.approx(1.0)

    up = UsdMetricsApi.GetEffectiveUpAxis(child)
    assert str(up) == "Y"


def test_grandchild_inherits_from_grandparent():
    """Grandchild inherits through intermediate prim with no metrics."""
    stage = Usd.Stage.CreateInMemory()
    gp = UsdGeom.Xform.Define(stage, "/GP").GetPrim()
    UsdGeom.Xform.Define(stage, "/GP/Parent")
    gc = UsdGeom.Xform.Define(stage, "/GP/Parent/GC").GetPrim()

    _apply(gp, meters_per_unit=0.001, up_axis="Z", kilograms_per_unit=1.0)

    assert UsdMetricsApi.GetEffectiveMetersPerUnit(gc) == pytest.approx(0.001)
    assert str(UsdMetricsApi.GetEffectiveUpAxis(gc)) == "Z"


def test_override():
    """Child metrics override parent's."""
    stage = Usd.Stage.CreateInMemory()
    parent = UsdGeom.Xform.Define(stage, "/Parent").GetPrim()
    child = UsdGeom.Xform.Define(stage, "/Parent/Child").GetPrim()

    _apply(parent, meters_per_unit=1.0, up_axis="Y", kilograms_per_unit=1.0)
    _apply(child, meters_per_unit=0.001)

    assert UsdMetricsApi.GetEffectiveMetersPerUnit(child) == pytest.approx(0.001)


# ---------------------------------------------------------------------------
# Fallback: stage-level layer metadata — mirrors test_layer_metadata_fallback
# ---------------------------------------------------------------------------

def test_layer_metadata_fallback():
    """No metrics on any prim → falls back to stage metadata."""
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageMetersPerUnit(stage, 0.01)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()

    mpu = UsdMetricsApi.GetEffectiveMetersPerUnit(prim)
    assert mpu == pytest.approx(0.01)

    up = UsdMetricsApi.GetEffectiveUpAxis(prim)
    assert str(up) == "Y"


def test_default_fallback():
    """No metrics, no stage metadata → USD defaults."""
    stage = Usd.Stage.CreateInMemory()
    prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()

    mpu = UsdMetricsApi.GetEffectiveMetersPerUnit(prim)
    assert mpu > 0

    up = UsdMetricsApi.GetEffectiveUpAxis(prim)
    assert str(up) in ("Y", "Z")

    kpu = UsdMetricsApi.GetEffectiveKilogramsPerUnit(prim)
    assert kpu > 0

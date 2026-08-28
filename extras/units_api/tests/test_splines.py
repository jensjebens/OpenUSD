"""Tests for animation curve (spline) unit conversion."""

import pytest
from pxr import Usd, UsdGeom, Gf, Ts, Sdf

from units_api import MetricsAPI, UnitsLens


def _make_bezier_spline(knots: list[tuple[float, float, float, float, float, float]]):
    """Create a Ts.Spline from (time, value, preSlope, preWidth, postSlope, postWidth) tuples."""
    spline = Ts.Spline()
    for time, value, pre_slope, pre_width, post_slope, post_width in knots:
        k = Ts.Knot()
        k.SetTime(time)
        k.SetValue(value)
        k.SetNextInterpolation(Ts.InterpCurve)
        k.SetPreTanSlope(pre_slope)
        k.SetPreTanWidth(pre_width)
        k.SetPostTanSlope(post_slope)
        k.SetPostTanWidth(post_width)
        spline.SetKnot(k)
    return spline


class TestSplineConversion:
    """Animation curve conversion: scale values and tangent slopes, preserve widths."""

    def test_get_spline_linear_attr(self):
        """Bezier spline on focusDistance (L¹) in cm → read in meters.
        Values and slopes scale by 0.01, widths unchanged."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm

        attr = prim.CreateAttribute("focusDistance", Sdf.ValueTypeNames.Double)
        spline = _make_bezier_spline([
            # time, value, preSlope, preWidth, postSlope, postWidth
            (1.0,   100.0,  0.0, 0.0,  20.0, 3.0),   # 100 cm, slope 20 cm/frame
            (10.0,  500.0,  10.0, 3.0,  0.0, 0.0),    # 500 cm, slope 10 cm/frame
        ])
        attr.SetSpline(spline)

        UnitsLens.clear_cache()
        result = UnitsLens.get_spline(attr, target_mpu=1.0)

        assert result is not None
        # Check knot values: 100 cm → 1 m, 500 cm → 5 m
        k1 = result.GetKnot(1.0)
        assert k1.GetValue() == pytest.approx(1.0)
        assert k1.GetPostTanSlope() == pytest.approx(0.2)   # 20 * 0.01
        assert k1.GetPostTanWidth() == pytest.approx(3.0)   # unchanged!

        k2 = result.GetKnot(10.0)
        assert k2.GetValue() == pytest.approx(5.0)
        assert k2.GetPreTanSlope() == pytest.approx(0.1)    # 10 * 0.01
        assert k2.GetPreTanWidth() == pytest.approx(3.0)    # unchanged!

    def test_spline_eval_matches_scaled_original(self):
        """Evaluating the converted spline at any time must equal
        original.Eval(t) * factor."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)

        attr = prim.CreateAttribute("focusDistance", Sdf.ValueTypeNames.Double)
        original = _make_bezier_spline([
            (1.0,  0.0,    0.0, 0.0, 20.0, 3.0),
            (10.0, 100.0,  5.0, 3.0, 0.0,  0.0),
        ])
        attr.SetSpline(original)

        UnitsLens.clear_cache()
        converted = UnitsLens.get_spline(attr, target_mpu=1.0)

        # Check at several evaluation points
        for t in [1.0, 3.0, 5.0, 5.5, 7.0, 9.0, 10.0]:
            original_val = original.Eval(t)
            converted_val = converted.Eval(t)
            expected = original_val * 0.01  # cm → m
            assert converted_val == pytest.approx(expected, abs=1e-10), \
                f"t={t}: converted={converted_val}, expected={expected}"

    def test_spline_no_spline_returns_none(self):
        """Attribute without spline → returns None."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)
        attr = prim.CreateAttribute("focusDistance", Sdf.ValueTypeNames.Double)
        attr.Set(500.0)

        result = UnitsLens.get_spline(attr, target_mpu=1.0)
        assert result is None

    def test_spline_unitless_unchanged(self):
        """Spline on unitless attribute → returned as-is."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)
        # Use a float attr that's unitless (like an opacity or blend weight)
        attr = prim.CreateAttribute("doubleSided", Sdf.ValueTypeNames.Double)
        spline = _make_bezier_spline([
            (1.0, 1.0, 0, 0, 0, 0),
            (10.0, 0.0, 0, 0, 0, 0),
        ])
        attr.SetSpline(spline)

        result = UnitsLens.get_spline(attr, target_mpu=1.0)
        # Should be same spline (no conversion)
        assert result.Eval(1.0) == pytest.approx(1.0)
        assert result.Eval(10.0) == pytest.approx(0.0)

    def test_set_spline_m_to_cm(self):
        """Author a spline in meters on a cm prim."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm

        attr = prim.CreateAttribute("focusDistance", Sdf.ValueTypeNames.Double)
        meter_spline = _make_bezier_spline([
            (1.0,  1.0,   0.0, 0.0, 0.2, 3.0),   # 1 m, slope 0.2 m/frame
            (10.0, 5.0,   0.1, 3.0, 0.0, 0.0),    # 5 m, slope 0.1 m/frame
        ])
        UnitsLens.clear_cache()
        UnitsLens.set_spline(attr, meter_spline, source_mpu=1.0)

        # Raw spline should be in cm
        raw = attr.GetSpline()
        k1 = raw.GetKnot(1.0)
        assert k1.GetValue() == pytest.approx(100.0)      # 1 m → 100 cm
        assert k1.GetPostTanSlope() == pytest.approx(20.0) # 0.2 → 20 cm/frame

    def test_set_get_spline_roundtrip(self):
        """Set spline in meters, get spline in meters → identical curve."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.001)  # mm

        attr = prim.CreateAttribute("focusDistance", Sdf.ValueTypeNames.Double)
        original = _make_bezier_spline([
            (1.0,  0.5,   0.0, 0.0, 0.1, 2.0),
            (24.0, 10.0,  0.05, 2.0, 0.0, 0.0),
        ])
        UnitsLens.clear_cache()
        UnitsLens.set_spline(attr, original, source_mpu=1.0)
        result = UnitsLens.get_spline(attr, target_mpu=1.0)

        # Evaluate at multiple times — should match original
        for t in [1.0, 5.0, 12.0, 18.0, 24.0]:
            assert result.Eval(t) == pytest.approx(original.Eval(t), abs=1e-9), \
                f"Roundtrip failed at t={t}"

    def test_spline_gravity_derived_quantity(self):
        """Spline on gravity (L¹·T⁻²): values and slopes scale correctly."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm

        attr = prim.CreateAttribute("physics:gravityMagnitude", Sdf.ValueTypeNames.Double)
        # Gravity animated (maybe changing planet?) 981 cm/s² → 490 cm/s²
        spline = _make_bezier_spline([
            (1.0,  981.0,  0.0, 0.0, -50.0, 3.0),
            (10.0, 490.0,  -50.0, 3.0, 0.0, 0.0),
        ])
        attr.SetSpline(spline)

        UnitsLens.clear_cache()
        result = UnitsLens.get_spline(attr, target_mpu=1.0)

        k1 = result.GetKnot(1.0)
        assert k1.GetValue() == pytest.approx(9.81)       # 981 cm/s² → 9.81 m/s²
        assert k1.GetPostTanSlope() == pytest.approx(-0.5) # -50 * 0.01

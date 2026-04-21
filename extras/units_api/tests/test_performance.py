"""Tests for performance optimizations and time-sampled attributes."""

import time
import pytest
from pxr import Usd, UsdGeom, Gf, Vt, Sdf

from units_api import MetricsAPI, UnitsLens, Dimension
from test_stages import build_stage1_bolt_in_factory, build_stage6_point_instancer


# ---------------------------------------------------------------------------
# Time-sampled attributes
# ---------------------------------------------------------------------------

class TestTimeSamples:
    """Reading and writing animated (time-sampled) attributes."""

    def setup_method(self):
        UnitsLens.clear_cache()

    def test_get_attr_at_specific_time(self):
        """get_attr with explicit time code reads the right sample."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm
        xf = UsdGeom.Xformable(prim)
        op = xf.AddTranslateOp()
        
        # Animate: frame 1 = (100,0,0) cm, frame 24 = (2400,0,0) cm
        for f in range(1, 25):
            op.Set(Gf.Vec3d(f * 100, 0, 0), f)
        
        attr = prim.GetAttribute("xformOp:translate")
        
        # Read frame 1 in meters
        v1 = UnitsLens.get_attr(attr, target_mpu=1.0, time=Usd.TimeCode(1))
        assert v1[0] == pytest.approx(1.0)  # 100 cm = 1 m
        
        # Read frame 24 in meters
        v24 = UnitsLens.get_attr(attr, target_mpu=1.0, time=Usd.TimeCode(24))
        assert v24[0] == pytest.approx(24.0)  # 2400 cm = 24 m

    def test_get_time_samples_bulk(self):
        """get_time_samples returns all time samples converted."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm
        xf = UsdGeom.Xformable(prim)
        op = xf.AddTranslateOp()

        for f in range(1, 25):
            op.Set(Gf.Vec3d(f * 100, 0, 0), f)

        attr = prim.GetAttribute("xformOp:translate")
        samples = UnitsLens.get_time_samples(attr, target_mpu=1.0)
        
        assert len(samples) == 24
        # Check first and last
        assert samples[0][0] == pytest.approx(1.0)  # time
        assert samples[0][1][0] == pytest.approx(1.0)  # 100 cm = 1 m
        assert samples[23][0] == pytest.approx(24.0)
        assert samples[23][1][0] == pytest.approx(24.0)  # 2400 cm = 24 m

    def test_get_time_samples_empty(self):
        """No time samples → empty list."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)
        attr = prim.CreateAttribute("test:val", Sdf.ValueTypeNames.Float)
        attr.Set(42.0)  # default only, no time samples

        samples = UnitsLens.get_time_samples(attr, target_mpu=1.0)
        assert samples == []

    def test_get_time_samples_unitless(self):
        """Unitless time-sampled attr → raw values returned."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)
        attr = prim.CreateAttribute("visibility", Sdf.ValueTypeNames.Token)
        attr.Set("inherited", 1)
        attr.Set("invisible", 10)

        samples = UnitsLens.get_time_samples(attr, target_mpu=1.0)
        assert len(samples) == 2
        assert samples[0][1] == "inherited"
        assert samples[1][1] == "invisible"

    def test_set_time_samples_bulk(self):
        """set_time_samples writes converted time samples."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm
        xf = UsdGeom.Xformable(prim)
        op = xf.AddTranslateOp()
        op.Set(Gf.Vec3d(0, 0, 0))  # init

        attr = prim.GetAttribute("xformOp:translate")
        
        # Author in meters
        meter_samples = [(float(f), Gf.Vec3d(f, 0, 0)) for f in range(1, 5)]
        UnitsLens.set_time_samples(attr, meter_samples, source_mpu=1.0)
        
        # Raw values should be in cm
        raw_f1 = attr.Get(1)
        assert raw_f1[0] == pytest.approx(100.0)  # 1 m → 100 cm
        raw_f4 = attr.Get(4)
        assert raw_f4[0] == pytest.approx(400.0)  # 4 m → 400 cm

    def test_set_get_samples_roundtrip(self):
        """Write samples in meters, read back in meters → same values."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.001)  # mm
        xf = UsdGeom.Xformable(prim)
        op = xf.AddTranslateOp()
        op.Set(Gf.Vec3d(0, 0, 0))
        
        attr = prim.GetAttribute("xformOp:translate")
        input_samples = [(1.0, Gf.Vec3d(0.5, 1.0, 2.0)),
                         (10.0, Gf.Vec3d(5.0, 10.0, 20.0))]
        UnitsLens.set_time_samples(attr, input_samples, source_mpu=1.0)

        output_samples = UnitsLens.get_time_samples(attr, target_mpu=1.0)
        assert len(output_samples) == 2
        for (in_t, in_v), (out_t, out_v) in zip(input_samples, output_samples):
            assert in_t == pytest.approx(out_t)
            for i in range(3):
                assert in_v[i] == pytest.approx(out_v[i], abs=1e-6)

    def test_animated_point_instancer(self):
        """PointInstancer with animated positions — read all frames in meters."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01)  # cm

        pi = UsdGeom.PointInstancer.Define(stage, "/Root/Instances")
        pi.GetProtoIndicesAttr().Set(Vt.IntArray([0, 0]))

        pos_attr = pi.GetPositionsAttr()
        # Frame 1: two instances at 100cm and 200cm
        pos_attr.Set(Vt.Vec3fArray([Gf.Vec3f(100, 0, 0), Gf.Vec3f(200, 0, 0)]), 1)
        # Frame 10: moved to 500cm and 1000cm
        pos_attr.Set(Vt.Vec3fArray([Gf.Vec3f(500, 0, 0), Gf.Vec3f(1000, 0, 0)]), 10)

        samples = UnitsLens.get_time_samples(pos_attr, target_mpu=1.0)
        assert len(samples) == 2

        # Frame 1 in meters
        t1, v1 = samples[0]
        assert t1 == pytest.approx(1.0)
        assert v1[0][0] == pytest.approx(1.0)   # 100 cm → 1 m
        assert v1[1][0] == pytest.approx(2.0)   # 200 cm → 2 m

        # Frame 10 in meters
        t10, v10 = samples[1]
        assert t10 == pytest.approx(10.0)
        assert v10[0][0] == pytest.approx(5.0)  # 500 cm → 5 m
        assert v10[1][0] == pytest.approx(10.0) # 1000 cm → 10 m


# ---------------------------------------------------------------------------
# Cache behavior
# ---------------------------------------------------------------------------

class TestCache:
    """Metrics cache correctness."""

    def test_cache_gives_same_result(self):
        """Cached result matches uncached result."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        
        UnitsLens.clear_cache()
        xf = UsdGeom.Xformable(shaft)
        op = xf.AddTranslateOp()
        op.Set(Gf.Vec3d(10, 0, 0))
        attr = shaft.GetAttribute("xformOp:translate")
        
        # First call populates cache
        v1 = UnitsLens.get_attr(attr, target_mpu=1.0)
        # Second call uses cache
        v2 = UnitsLens.get_attr(attr, target_mpu=1.0)
        
        assert v1[0] == pytest.approx(v2[0])

    def test_clear_cache_after_edit(self):
        """After editing metrics and clearing cache, new values are used."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)  # cm
        attr = prim.CreateAttribute("focusDistance", Sdf.ValueTypeNames.Float)
        attr.Set(500.0)

        UnitsLens.clear_cache()
        v1 = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert v1 == pytest.approx(5.0)  # 500 cm → 5 m

        # Change units to mm
        MetricsAPI.apply(prim, meters_per_unit=0.001)
        UnitsLens.clear_cache()
        v2 = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert v2 == pytest.approx(0.5)  # 500 mm → 0.5 m


# ---------------------------------------------------------------------------
# Performance smoke tests (not strict, just ensure numpy path works)
# ---------------------------------------------------------------------------

class TestNumpyFastPath:
    """Verify numpy acceleration produces correct results."""

    def test_large_vec3f_array_correct(self):
        """100k element Vec3fArray converts correctly with numpy."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)

        n = 100000
        positions = Vt.Vec3fArray([Gf.Vec3f(i, i * 2, i * 3) for i in range(n)])
        attr = prim.CreateAttribute("positions", Sdf.ValueTypeNames.Point3fArray)
        attr.Set(positions)

        UnitsLens.clear_cache()
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert len(result) == n
        # Spot check: element 1000 = (1000, 2000, 3000) cm → (10, 20, 30) m
        assert result[1000][0] == pytest.approx(10.0, abs=0.01)
        assert result[1000][1] == pytest.approx(20.0, abs=0.01)
        assert result[1000][2] == pytest.approx(30.0, abs=0.01)

    def test_float_array_correct(self):
        """FloatArray converts correctly via numpy."""
        stage = Usd.Stage.CreateInMemory()
        prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()
        MetricsAPI.apply(prim, meters_per_unit=0.01)

        vals = Vt.FloatArray([float(i) for i in range(1000)])
        attr = prim.CreateAttribute("extent", Sdf.ValueTypeNames.FloatArray)
        attr.Set(vals)

        UnitsLens.clear_cache()
        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        # element 100 = 100 cm → 1.0 m
        assert result[100] == pytest.approx(1.0, abs=0.001)

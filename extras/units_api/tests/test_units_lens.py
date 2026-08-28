"""Tests for UnitsLens — read/author with unit conversion."""

import math
import pytest
from pxr import Usd, UsdGeom, Sdf, Gf, Vt
from units_api import MetricsAPI, Dimension
from units_api.units_lens import UnitsLens
from test_stages import (
    build_stage1_bolt_in_factory,
    build_stage2_physics,
    build_stage3_camera_lights,
    build_stage5_custom_attrs,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _approx_vec3(a, b, rel=1e-5):
    """Component-wise approximate equality for Gf.Vec3* and Vec3f."""
    return all(math.isclose(a[i], b[i], rel_tol=rel) for i in range(3))


# ---------------------------------------------------------------------------
# Stage 1: Simple linear conversions
# ---------------------------------------------------------------------------

class TestStage1Linear:

    def setup_method(self):
        self.stage = build_stage1_bolt_in_factory()
        self.shaft = self.stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        self.factory = self.stage.GetPrimAtPath("/Factory")

    def test_read_translate_mm_to_m(self):
        """Bolt shaft translate (10, 0, 0) in mm → (0.01, 0, 0) in meters."""
        attr = self.shaft.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(10.0, 0.0, 0.0))

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert _approx_vec3(result, (0.01, 0.0, 0.0))

    def test_read_extent_mm_to_m(self):
        """Bolt shaft extent in mm → meters."""
        attr = self.shaft.CreateAttribute("extent", Sdf.ValueTypeNames.Float3Array)
        attr.Set(Vt.Vec3fArray([Gf.Vec3f(0, 0, 0), Gf.Vec3f(5.0, 2.0, 1.0)]))

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert isinstance(result, Vt.Vec3fArray)
        assert _approx_vec3(result[0], (0.0, 0.0, 0.0))
        assert _approx_vec3(result[1], (0.005, 0.002, 0.001))

    def test_read_no_conversion_same_units(self):
        """Factory prim in meters, reading in meters → value unchanged."""
        attr = self.factory.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(5.0, 0.0, 0.0))

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert _approx_vec3(result, (5.0, 0.0, 0.0))

    def test_read_in_meters_convenience(self):
        """read_in_meters convenience wrapper."""
        attr = self.shaft.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(1000.0, 0.0, 0.0))  # 1000 mm

        result = UnitsLens.get_in_meters(attr)

        assert math.isclose(result[0], 1.0, rel_tol=1e-5)


# ---------------------------------------------------------------------------
# Stage 2: Derived quantities (physics)
# ---------------------------------------------------------------------------

class TestStage2Physics:

    def setup_method(self):
        self.stage = build_stage2_physics()

    def test_read_density_m_authored_read_in_cm(self):
        """Density 2700 kg/m³ authored in meter context, read in cm → 0.0027 kg/cm³."""
        gripper = self.stage.GetPrimAtPath("/World/Robot/Gripper")
        attr = gripper.GetAttribute("physics:density")

        # source_mpu=1.0 (gripper in m), target_mpu=0.01 (cm)
        # factor = (1.0/0.01)^(-3) = 100^(-3) = 1e-6
        result = UnitsLens.get_attr(attr, target_mpu=0.01)

        assert math.isclose(result, 2700.0 * 1e-6, rel_tol=1e-5)

    def test_read_density_in_meters(self):
        """Density 2700 kg/m³ authored in meter context, read in meters → unchanged."""
        gripper = self.stage.GetPrimAtPath("/World/Robot/Gripper")
        attr = gripper.GetAttribute("physics:density")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 2700.0, rel_tol=1e-5)

    def test_read_gravity_cm_to_m(self):
        """gravityMagnitude=981 cm/s² in cm stage, read in meters → 9.81 m/s²."""
        scene = self.stage.GetPrimAtPath("/World/PhysicsScene")
        attr = scene.GetAttribute("physics:gravityMagnitude")

        # source_mpu=0.01 (world cm context), target_mpu=1.0
        # factor = (0.01/1.0)^1 = 0.01
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 9.81, rel_tol=1e-4)

    def test_read_velocity_m_authored_read_in_cm(self):
        """Velocity (1,0,0) m/s authored in meter context, read in cm → (100,0,0) cm/s."""
        arm = self.stage.GetPrimAtPath("/World/Robot/Arm")
        attr = arm.GetAttribute("physics:velocity")

        # source_mpu=1.0 (robot m), target_mpu=0.01 (cm)
        # factor = (1.0/0.01)^1 = 100
        result = UnitsLens.get_attr(attr, target_mpu=0.01)

        assert _approx_vec3(result, (100.0, 0.0, 0.0))

    def test_read_mass_unchanged(self):
        """Mass (M¹, L=0) should be unchanged by linear unit conversion."""
        arm = self.stage.GetPrimAtPath("/World/Robot/Arm")
        attr = arm.GetAttribute("physics:mass")

        # target_mpu doesn't matter for M-only dimension when kpu stays 1:1
        result_m = UnitsLens.get_attr(attr, target_mpu=1.0)
        result_cm = UnitsLens.get_attr(attr, target_mpu=0.01)

        assert math.isclose(result_m, 10.0, rel_tol=1e-5)
        assert math.isclose(result_cm, 10.0, rel_tol=1e-5)

    def test_read_mass_kpu_conversion(self):
        """Mass converts correctly when kilogramsPerUnit differs."""
        arm = self.stage.GetPrimAtPath("/World/Robot/Arm")
        attr = arm.GetAttribute("physics:mass")

        # source_kpu=1.0 (kg), target_kpu=1000.0 (tonnes) → factor = 1/1000
        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_kpu=1000.0)

        assert math.isclose(result, 0.01, rel_tol=1e-5)


# ---------------------------------------------------------------------------
# Stage 3: Camera & lights
# ---------------------------------------------------------------------------

class TestStage3CameraLights:

    def setup_method(self):
        self.stage = build_stage3_camera_lights()

    def test_read_focus_distance_cm_to_m(self):
        """focusDistance=500 cm → 5.0 m."""
        camera = self.stage.GetPrimAtPath("/Scene/Camera")
        attr = camera.GetAttribute("focusDistance")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 5.0, rel_tol=1e-5)

    def test_read_clipping_range_cm_to_m(self):
        """clippingRange=(1, 100000) cm → (0.01, 1000) m."""
        camera = self.stage.GetPrimAtPath("/Scene/Camera")
        attr = camera.GetAttribute("clippingRange")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert isinstance(result, Gf.Vec2f)
        assert math.isclose(result[0], 0.01, rel_tol=1e-4)
        assert math.isclose(result[1], 1000.0, rel_tol=1e-4)

    def test_read_focal_length_not_converted(self):
        """focalLength is NOT in dimension registry → returned unchanged."""
        camera = self.stage.GetPrimAtPath("/Scene/Camera")
        attr = camera.GetAttribute("focalLength")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 50.0, rel_tol=1e-5)

    def test_read_horizontal_aperture_not_converted(self):
        """horizontalAperture is NOT in registry → returned unchanged."""
        camera = self.stage.GetPrimAtPath("/Scene/Camera")
        attr = camera.GetAttribute("horizontalAperture")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 36.0, rel_tol=1e-5)

    def test_read_light_width_cm_to_m(self):
        """inputs:width=100 cm → 1.0 m."""
        light = self.stage.GetPrimAtPath("/Scene/KeyLight")
        attr = light.GetAttribute("inputs:width")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 1.0, rel_tol=1e-5)

    def test_read_light_height_cm_to_m(self):
        """inputs:height=100 cm → 1.0 m."""
        light = self.stage.GetPrimAtPath("/Scene/KeyLight")
        attr = light.GetAttribute("inputs:height")

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert math.isclose(result, 1.0, rel_tol=1e-5)


# ---------------------------------------------------------------------------
# Stage 5: Custom attributes
# ---------------------------------------------------------------------------

class TestStage5CustomAttrs:

    def setup_method(self):
        self.stage = build_stage5_custom_attrs()
        self.segment = self.stage.GetPrimAtPath("/Pipe/Segment")

    def test_read_unknown_custom_attr_passthrough(self):
        """Custom attrs not in registry → returned unchanged regardless of target_mpu."""
        inner_r = self.segment.GetAttribute("myPipeline:innerRadius")

        result_m = UnitsLens.get_attr(inner_r, target_mpu=1.0)
        result_cm = UnitsLens.get_attr(inner_r, target_mpu=0.01)

        assert math.isclose(result_m, 0.05, rel_tol=1e-5)
        assert math.isclose(result_cm, 0.05, rel_tol=1e-5)

    def test_read_all_custom_attrs_unchanged(self):
        """All custom pipeline attrs pass through (none are in the registry)."""
        expected = {
            "myPipeline:innerRadius": 0.05,
            "myPipeline:outerRadius": 0.06,
            "myPipeline:flowRate": 0.002,
            "myPipeline:pressure": 101325.0,
            "myPipeline:roughnessCoeff": 0.015,
        }
        for name, expected_val in expected.items():
            attr = self.segment.GetAttribute(name)
            result = UnitsLens.get_attr(attr, target_mpu=1.0)
            assert math.isclose(result, expected_val, rel_tol=1e-5), \
                f"{name}: expected {expected_val}, got {result}"


# ---------------------------------------------------------------------------
# Authoring
# ---------------------------------------------------------------------------

class TestAuthoring:

    def test_author_translate_m_to_cm(self):
        """Author a 5m translate on a cm prim → stored as (500, 0, 0)."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01)

        attr = root.GetPrim().CreateAttribute("xformOp:translate",
                                              Sdf.ValueTypeNames.Double3)
        UnitsLens.set_attr(attr, Gf.Vec3d(5.0, 0.0, 0.0), source_mpu=1.0)

        stored = attr.Get()
        assert _approx_vec3(stored, (500.0, 0.0, 0.0))

    def test_author_density_m_to_cm(self):
        """Author density 2700 kg/m³ on a cm prim → stored in cm units (0.0027 kg/cm³)."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01, kilograms_per_unit=1.0)

        attr = root.GetPrim().CreateAttribute("physics:density",
                                              Sdf.ValueTypeNames.Float)
        UnitsLens.set_attr(attr, 2700.0, source_mpu=1.0, source_kpu=1.0)

        stored = attr.Get()
        # factor = (1.0/0.01)^(-3) = 1e-6 → 2700 * 1e-6 = 0.0027
        assert math.isclose(stored, 0.0027, rel_tol=1e-4)

    def test_author_roundtrip(self):
        """Author a value then read it back → original value recovered."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01)

        attr = root.GetPrim().CreateAttribute("xformOp:translate",
                                              Sdf.ValueTypeNames.Double3)
        original = Gf.Vec3d(3.0, 1.5, 0.5)
        UnitsLens.set_attr(attr, original, source_mpu=1.0)

        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert _approx_vec3(result, original)

    def test_author_unitless_passthrough(self):
        """Author on a unitless/unknown attr → stored verbatim."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01)

        attr = root.GetPrim().CreateAttribute("visibility", Sdf.ValueTypeNames.Token)
        UnitsLens.set_attr(attr, "inherited", source_mpu=1.0)

        assert attr.Get() == "inherited"


# ---------------------------------------------------------------------------
# Edge cases
# ---------------------------------------------------------------------------

class TestEdgeCases:

    def test_read_unitless_attr_unchanged(self):
        """Unitless attr (doubleSided) → returned unchanged."""
        stage = Usd.Stage.CreateInMemory()
        mesh = UsdGeom.Mesh.Define(stage, "/M")
        MetricsAPI.apply(mesh.GetPrim(), meters_per_unit=0.01)
        attr = mesh.GetDoubleSidedAttr()
        attr.Set(True)

        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert result is True

    def test_read_no_value_returns_none(self):
        """Attribute with no authored value → returns None."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=1.0)
        attr = root.GetPrim().CreateAttribute("xformOp:translate",
                                              Sdf.ValueTypeNames.Double3)
        # Do NOT set a value

        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert result is None

    def test_read_attr_not_in_registry(self):
        """Attr name not in registry → raw value returned unchanged."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01)
        attr = root.GetPrim().CreateAttribute("myCustom:thing",
                                              Sdf.ValueTypeNames.Float)
        attr.Set(42.0)

        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert math.isclose(result, 42.0, rel_tol=1e-5)

    def test_read_scalar_float(self):
        """Scalar float attribute (size) converts correctly."""
        stage = Usd.Stage.CreateInMemory()
        cube = UsdGeom.Cube.Define(stage, "/Cube")
        MetricsAPI.apply(cube.GetPrim(), meters_per_unit=0.001)  # mm
        cube.GetSizeAttr().Set(100.0)  # 100 mm

        result = UnitsLens.get_attr(cube.GetSizeAttr(), target_mpu=1.0)

        assert math.isclose(result, 0.1, rel_tol=1e-5)  # 0.1 m

    def test_read_no_metrics_falls_back_to_stage(self):
        """Prim with no MetricsAPI → falls back to stage-level metersPerUnit."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 0.01)  # cm stage
        root = UsdGeom.Xform.Define(stage, "/Root")
        # No MetricsAPI applied
        attr = root.GetPrim().CreateAttribute("xformOp:translate",
                                              Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(500.0, 0.0, 0.0))  # 500 cm

        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert _approx_vec3(result, (5.0, 0.0, 0.0))  # 5 m


# ---------------------------------------------------------------------------
# Debug helper
# ---------------------------------------------------------------------------

class TestConversionInfo:

    def test_conversion_info_structure(self):
        """get_conversion_info returns a dict with expected keys."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        attr = shaft.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(10.0, 0.0, 0.0))

        info = UnitsLens.get_conversion_info(attr)

        assert info["attr_name"] == "xformOp:translate"
        assert math.isclose(info["source_mpu"], 0.001, rel_tol=1e-5)
        assert info["dimension"] is not None
        assert "conversion_factor_to_meters" in info

    def test_conversion_info_factor_correct(self):
        """conversion_factor_to_meters matches expected for mm→m."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        attr = shaft.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(1.0, 0.0, 0.0))

        info = UnitsLens.get_conversion_info(attr)

        # mm (0.001) to m: factor = 0.001
        assert math.isclose(info["conversion_factor_to_meters"], 0.001, rel_tol=1e-5)

    def test_conversion_info_unknown_attr(self):
        """Unknown attr → factor is 1.0, dimension is None."""
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=1.0)
        attr = root.GetPrim().CreateAttribute("unknown:thing", Sdf.ValueTypeNames.Float)
        attr.Set(1.0)

        info = UnitsLens.get_conversion_info(attr)

        assert info["dimension"] is None
        assert math.isclose(info["conversion_factor_to_meters"], 1.0, rel_tol=1e-5)


# ---------------------------------------------------------------------------
# xformOp convenience methods
# ---------------------------------------------------------------------------

class TestXformConvenience:
    """Unit-aware xformOp authoring via the lens."""

    def test_set_translate_m_on_mm_prim(self):
        """Set translate (0.01, 0, 0) meters on mm bolt → stored as (10, 0, 0)"""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        UnitsLens.set_translate(shaft, Gf.Vec3d(0.01, 0, 0), source_mpu=1.0)
        xformable = UsdGeom.Xformable(shaft)
        ops = xformable.GetOrderedXformOps()
        translate_op = [op for op in ops if "translate" in op.GetOpName().lower()][0]
        raw = translate_op.Get()
        assert raw[0] == pytest.approx(10.0, abs=1e-6)
        assert raw[1] == pytest.approx(0.0)
        assert raw[2] == pytest.approx(0.0)

    def test_set_translate_m_on_m_prim(self):
        """Set translate on meter prim → stored unchanged"""
        stage = build_stage1_bolt_in_factory()
        floor = stage.GetPrimAtPath("/Factory/Floor")
        UnitsLens.set_translate(floor, Gf.Vec3d(5, 0, 0), source_mpu=1.0)
        xformable = UsdGeom.Xformable(floor)
        ops = xformable.GetOrderedXformOps()
        translate_op = [op for op in ops if "translate" in op.GetOpName().lower()][0]
        raw = translate_op.Get()
        assert raw[0] == pytest.approx(5.0)

    def test_get_translate_mm_to_m(self):
        """Read translate from mm prim, get result in meters"""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        xformable = UsdGeom.Xformable(shaft)
        op = xformable.AddTranslateOp()
        op.Set(Gf.Vec3d(10, 0, 0))  # 10mm
        result = UnitsLens.get_translate(shaft, target_mpu=1.0)
        assert result[0] == pytest.approx(0.01)  # 10mm = 0.01m

    def test_get_translate_no_op(self):
        """Prim with no translate op → returns None"""
        stage = build_stage1_bolt_in_factory()
        prim = stage.GetPrimAtPath("/Factory/Equipment")
        result = UnitsLens.get_translate(prim, target_mpu=1.0)
        assert result is None

    def test_set_scale_unitless(self):
        """Scale is unitless — no conversion applied"""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        UnitsLens.set_scale(shaft, Gf.Vec3d(2, 2, 2))
        xformable = UsdGeom.Xformable(shaft)
        ops = xformable.GetOrderedXformOps()
        scale_op = [op for op in ops if "scale" in op.GetOpName().lower()][0]
        raw = scale_op.Get()
        assert raw[0] == pytest.approx(2.0)

    def test_get_world_position_after_assembly_correction(self):
        """World position accounts for assembly corrective transforms.
        Bolt in mm with assembly correction → world position in meters is correct."""
        from units_api import MetricsAssembler
        stage = build_stage1_bolt_in_factory()
        bolt = stage.GetPrimAtPath("/Factory/Equipment/Bolt")
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")

        MetricsAssembler.correct_reference_boundary(bolt)

        xformable = UsdGeom.Xformable(shaft)
        op = xformable.AddTranslateOp()
        op.Set(Gf.Vec3d(10, 0, 0))  # 10mm

        world_pos = UnitsLens.get_world_position(shaft, target_mpu=1.0)
        assert world_pos[0] == pytest.approx(0.01, abs=1e-6)

    def test_roundtrip_set_get_translate(self):
        """Set translate in meters, get it back in meters → same value"""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        UnitsLens.set_translate(shaft, Gf.Vec3d(0.05, 0.1, 0.02), source_mpu=1.0)
        result = UnitsLens.get_translate(shaft, target_mpu=1.0)
        assert result[0] == pytest.approx(0.05, abs=1e-9)
        assert result[1] == pytest.approx(0.1, abs=1e-9)
        assert result[2] == pytest.approx(0.02, abs=1e-9)

    def test_get_world_position_no_correction(self):
        """World position without assembly correction — uses root metrics.
        The stage root is in meters (mpu=1.0), so if we set floor translate to (5,0,0)
        in meters, world pos should be (5,0,0) meters."""
        stage = build_stage1_bolt_in_factory()
        floor = stage.GetPrimAtPath("/Factory/Floor")
        xformable = UsdGeom.Xformable(floor)
        op = xformable.AddTranslateOp()
        op.Set(Gf.Vec3d(5, 0, 0))
        world_pos = UnitsLens.get_world_position(floor, target_mpu=1.0)
        assert world_pos[0] == pytest.approx(5.0, abs=1e-6)


# ---------------------------------------------------------------------------
# Hybrid fallback: registry → per-attribute → passthrough
# ---------------------------------------------------------------------------

class TestHybridFallback:
    """UnitsLens falls back to per-attribute annotation for unknown attributes."""

    def test_custom_attr_with_annotation_via_lens(self):
        """Custom attr annotated with PerAttributeUnits → UnitsLens.get_attr converts it.
        This is the hybrid: registry miss → per-attribute fallback → conversion works."""
        from units_api.per_attribute import PerAttributeUnits
        stage = build_stage5_custom_attrs()
        seg_prim = stage.GetPrimAtPath("/Pipe/Segment")
        flow_attr = seg_prim.GetAttribute("myPipeline:flowRate")
        raw = flow_attr.Get()  # 0.002 m³/s

        PerAttributeUnits.annotate(flow_attr, Dimension(L=3, T=-1), meters_per_unit=1.0)

        # Registry miss → per-attribute fallback → converts volumetric flow
        # factor = (1.0/0.01)^3 = 1e6
        result = UnitsLens.get_attr(flow_attr, target_mpu=0.01)
        assert math.isclose(result, raw * 1e6, rel_tol=1e-5)

    def test_custom_attr_without_annotation_via_lens(self):
        """Custom attr with no annotation → still passthrough (no change from before)."""
        stage = build_stage5_custom_attrs()
        seg_prim = stage.GetPrimAtPath("/Pipe/Segment")
        flow_attr = seg_prim.GetAttribute("myPipeline:flowRate")
        raw = flow_attr.Get()

        result_m = UnitsLens.get_attr(flow_attr, target_mpu=1.0)
        result_cm = UnitsLens.get_attr(flow_attr, target_mpu=0.01)
        assert math.isclose(result_m, raw, rel_tol=1e-5)
        assert math.isclose(result_cm, raw, rel_tol=1e-5)

    def test_registry_attr_ignores_per_attribute(self):
        """Known registry attr (focusDistance) uses registry, not per-attribute,
        even if per-attribute annotation exists. Registry takes precedence."""
        from units_api.per_attribute import PerAttributeUnits
        stage = build_stage3_camera_lights()
        camera = stage.GetPrimAtPath("/Scene/Camera")
        focus_attr = camera.GetAttribute("focusDistance")

        # Annotate with intentionally wrong dimension (L3 instead of L1)
        PerAttributeUnits.annotate(focus_attr, Dimension(L=3), meters_per_unit=0.01)

        # Registry has focusDistance as L1 — that must take precedence
        # 500 cm × (0.01/1.0)^1 = 5.0 m
        result = UnitsLens.get_attr(focus_attr, target_mpu=1.0)
        assert math.isclose(result, 5.0, rel_tol=1e-5)

    def test_set_attr_hybrid_fallback(self):
        """set_attr also falls back to per-attribute for unknown attrs."""
        from units_api.per_attribute import PerAttributeUnits
        stage = build_stage5_custom_attrs()
        seg_prim = stage.GetPrimAtPath("/Pipe/Segment")
        inner_r = seg_prim.GetAttribute("myPipeline:innerRadius")

        # Annotate: attribute stores values in meters (mpu=1.0)
        PerAttributeUnits.annotate(inner_r, Dimension(L=1), meters_per_unit=1.0)

        # Author 0.05 m from meter source → stored as 0.05
        UnitsLens.set_attr(inner_r, 0.05, source_mpu=1.0)
        assert math.isclose(inner_r.Get(), 0.05, rel_tol=1e-5)

        # Author 5 cm (source_mpu=0.01) → stored as 0.05 m
        UnitsLens.set_attr(inner_r, 5.0, source_mpu=0.01)
        assert math.isclose(inner_r.Get(), 0.05, rel_tol=1e-5)

    def test_conversion_info_reports_source(self):
        """get_conversion_info shows whether registry or per-attribute was used."""
        from units_api.per_attribute import PerAttributeUnits
        stage = build_stage5_custom_attrs()
        seg_prim = stage.GetPrimAtPath("/Pipe/Segment")
        flow_attr = seg_prim.GetAttribute("myPipeline:flowRate")

        # No annotation → passthrough
        info = UnitsLens.get_conversion_info(flow_attr)
        assert info["unit_source"] == "passthrough"

        # With annotation → per_attribute
        PerAttributeUnits.annotate(flow_attr, Dimension(L=3, T=-1), meters_per_unit=1.0)
        info = UnitsLens.get_conversion_info(flow_attr)
        assert info["unit_source"] == "per_attribute"
        assert info["dimension"] == Dimension(L=3, T=-1)

        # Registry attr → registry
        cam_stage = build_stage3_camera_lights()
        focus_attr = cam_stage.GetPrimAtPath("/Scene/Camera").GetAttribute("focusDistance")
        info = UnitsLens.get_conversion_info(focus_attr)
        assert info["unit_source"] == "registry"


class TestMatrixTransform:
    """xformOp:transform — matrix conversion scales translation only."""

    def test_matrix_translate_only(self):
        """Matrix with only translation: translation scales, rest unchanged."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        xformable = UsdGeom.Xformable(shaft)
        op = xformable.AddTransformOp()
        m = Gf.Matrix4d()
        m.SetTranslate(Gf.Vec3d(10, 20, 30))  # mm
        op.Set(m)

        attr = shaft.GetAttribute("xformOp:transform")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)  # mm → m

        assert isinstance(result, Gf.Matrix4d)
        t = result.ExtractTranslation()
        assert t[0] == pytest.approx(0.01)   # 10 mm → 0.01 m
        assert t[1] == pytest.approx(0.02)
        assert t[2] == pytest.approx(0.03)

    def test_matrix_rotation_preserved(self):
        """Rotation component of matrix must NOT be scaled."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        xformable = UsdGeom.Xformable(shaft)
        op = xformable.AddTransformOp()

        m = Gf.Matrix4d()
        m.SetRotate(Gf.Rotation(Gf.Vec3d(0, 0, 1), 45.0))
        m.SetTranslateOnly(Gf.Vec3d(100, 0, 0))  # 100 mm
        op.Set(m)

        attr = shaft.GetAttribute("xformOp:transform")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        # Translation scaled
        t = result.ExtractTranslation()
        assert t[0] == pytest.approx(0.1)  # 100 mm → 0.1 m

        # Rotation preserved — extract rotation angle around Z
        rot = result.ExtractRotation()
        assert rot.GetAngle() == pytest.approx(45.0, abs=1e-6)
        assert rot.GetAxis()[2] == pytest.approx(1.0, abs=1e-6)

    def test_matrix_scale_preserved(self):
        """Scale component of matrix must NOT be affected by unit conversion."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        xformable = UsdGeom.Xformable(shaft)
        op = xformable.AddTransformOp()

        m = Gf.Matrix4d()
        m.SetScale(Gf.Vec3d(2, 3, 4))
        m.SetTranslateOnly(Gf.Vec3d(50, 0, 0))  # 50 mm
        op.Set(m)

        attr = shaft.GetAttribute("xformOp:transform")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        t = result.ExtractTranslation()
        assert t[0] == pytest.approx(0.05)  # 50 mm → 0.05 m

        # Scale should be unchanged
        # Extract scale by looking at row magnitudes (rows 0-2)
        for i in range(3):
            row = result.GetRow(i)
            orig_row = m.GetRow(i)
            assert Gf.Vec3d(row[0], row[1], row[2]).GetLength() == pytest.approx(
                Gf.Vec3d(orig_row[0], orig_row[1], orig_row[2]).GetLength(), abs=1e-9
            )

    def test_matrix_identity_no_change(self):
        """Identity matrix in same-unit context → unchanged."""
        stage = build_stage1_bolt_in_factory()
        floor = stage.GetPrimAtPath("/Factory/Floor")
        xformable = UsdGeom.Xformable(floor)
        op = xformable.AddTransformOp()
        m = Gf.Matrix4d(1.0)
        op.Set(m)

        attr = floor.GetAttribute("xformOp:transform")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        assert result == m  # floor is meters, target is meters

    def test_matrix_set_attr_roundtrip(self):
        """set_attr in meters → get_attr in meters → same translation."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
        xformable = UsdGeom.Xformable(shaft)
        op = xformable.AddTransformOp()
        op.Set(Gf.Matrix4d(1.0))  # init

        attr = shaft.GetAttribute("xformOp:transform")

        m = Gf.Matrix4d()
        m.SetTranslate(Gf.Vec3d(0.05, 0.1, 0.02))  # meters
        UnitsLens.set_attr(attr, m, source_mpu=1.0)  # shaft is mm → stores mm values

        # Read back in meters
        result = UnitsLens.get_attr(attr, target_mpu=1.0)
        t = result.ExtractTranslation()
        assert t[0] == pytest.approx(0.05, abs=1e-9)
        assert t[1] == pytest.approx(0.1, abs=1e-9)
        assert t[2] == pytest.approx(0.02, abs=1e-9)

    def test_matrix_array_skel_style(self):
        """VtMatrix4dArray (like Skel restTransforms) — each matrix's
        translation is scaled, rotations preserved."""
        stage = build_stage1_bolt_in_factory()
        shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")

        # Create a custom attr to hold a matrix array (like restTransforms)
        attr = shaft.CreateAttribute("test:restTransforms", 
                                      Sdf.ValueTypeNames.Matrix4dArray)

        m1 = Gf.Matrix4d()
        m1.SetTranslate(Gf.Vec3d(10, 0, 0))  # 10 mm

        m2 = Gf.Matrix4d()
        m2.SetRotate(Gf.Rotation(Gf.Vec3d(1, 0, 0), 90.0))
        m2.SetTranslateOnly(Gf.Vec3d(0, 20, 0))  # 20 mm

        attr.Set(Vt.Matrix4dArray([m1, m2]))

        # Annotate with per-attribute (not in registry)
        from units_api import PerAttributeUnits, Dimension
        PerAttributeUnits.annotate(attr, Dimension(L=1), meters_per_unit=0.001)

        # Read via hybrid fallback
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert len(result) == 2
        t1 = result[0].ExtractTranslation()
        assert t1[0] == pytest.approx(0.01)  # 10 mm → 0.01 m

        t2 = result[1].ExtractTranslation()
        assert t2[1] == pytest.approx(0.02)  # 20 mm → 0.02 m

        # Rotation in m2 preserved
        rot = result[1].ExtractRotation()
        assert rot.GetAngle() == pytest.approx(90.0, abs=1e-6)
        assert rot.GetAxis()[0] == pytest.approx(1.0, abs=1e-6)

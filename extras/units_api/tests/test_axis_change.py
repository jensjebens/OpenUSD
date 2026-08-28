"""Tests for axis change support in UnitsLens and dimensions."""

import math
import pytest
from pxr import Usd, UsdGeom, Sdf, Gf, Vt

from units_api import (
    MetricsAPI, Dimension, UnitsLens,
    AxisTransform, AXIS_TRANSFORM_REGISTRY, get_axis_transform,
    axis_rotation_matrix, remap_axis_token,
    DIMENSION_REGISTRY,
)
from test_stages import build_stage2_physics


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _approx_vec3(a, b, rel=1e-5):
    return all(math.isclose(a[i], b[i], rel_tol=rel, abs_tol=1e-9) for i in range(3))


def _make_yup_stage():
    """Simple Y-up stage in meters with a prim."""
    UnitsLens.clear_cache()
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    root = UsdGeom.Xform.Define(stage, "/Root")
    MetricsAPI.apply(root.GetPrim(), meters_per_unit=1.0, up_axis="Y")
    return stage


def _make_zup_stage():
    """Simple Z-up stage in meters with a prim."""
    UnitsLens.clear_cache()
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    root = UsdGeom.Xform.Define(stage, "/Root")
    MetricsAPI.apply(root.GetPrim(), meters_per_unit=1.0, up_axis="Z")
    return stage


# ---------------------------------------------------------------------------
# Rotation mapping reference (verified empirically):
#
#   Y->Z (-90 deg around X):
#     X -> X
#     Y -> Z       i.e. (0,1,0) -> (0,~0,1)
#     Z -> -Y      i.e. (0,0,1) -> (0,-1,~0)
#     So (x,y,z) -> (x, -z, y)
#
#   Z->Y (+90 deg around X):
#     X -> X
#     Y -> -Z      i.e. (0,1,0) -> (0,~0,-1)
#     Z -> Y       i.e. (0,0,1) -> (0,1,~0)
#     So (x,y,z) -> (x, z, -y)
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# AxisTransform registry completeness
# ---------------------------------------------------------------------------

class TestRegistryCompleteness:
    """Every attribute in DIMENSION_REGISTRY must also be in AXIS_TRANSFORM_REGISTRY."""

    def test_all_dimension_attrs_have_axis_transform(self):
        missing = [
            name for name in DIMENSION_REGISTRY
            if name not in AXIS_TRANSFORM_REGISTRY
        ]
        assert missing == [], f"Attrs in DIMENSION_REGISTRY but not AXIS_TRANSFORM_REGISTRY: {missing}"

    def test_get_axis_transform_known(self):
        assert get_axis_transform("physics:velocity") == AxisTransform.VECTOR3
        assert get_axis_transform("physics:mass") == AxisTransform.NONE
        assert get_axis_transform("points") == AxisTransform.VECTOR3

    def test_get_axis_transform_unknown(self):
        assert get_axis_transform("some:unknown:attr") is None


# ---------------------------------------------------------------------------
# axis_rotation_matrix
# ---------------------------------------------------------------------------

class TestAxisRotationMatrix:

    def test_y_to_z_rotation(self):
        rot = axis_rotation_matrix("Y", "Z")
        assert rot is not None
        # Y-up -> Z-up: (0,1,0) should map to (0,~0,1)
        v_y = Gf.Vec3d(0, 1, 0)
        result = rot * v_y
        assert _approx_vec3(result, (0, 0, 1))

    def test_z_to_y_rotation(self):
        rot = axis_rotation_matrix("Z", "Y")
        assert rot is not None
        # Z-up -> Y-up: (0,0,1) should map to (0,1,~0)
        v_z = Gf.Vec3d(0, 0, 1)
        result = rot * v_z
        assert _approx_vec3(result, (0, 1, 0))

    def test_same_axis_returns_none(self):
        assert axis_rotation_matrix("Y", "Y") is None
        assert axis_rotation_matrix("Z", "Z") is None

    def test_x_preserved(self):
        """X component unchanged under Y<->Z rotation."""
        rot = axis_rotation_matrix("Y", "Z")
        v = Gf.Vec3d(1, 0, 0)
        result = rot * v
        assert _approx_vec3(result, (1, 0, 0))

    def test_roundtrip_y_z_y(self):
        """Y->Z->Y should give identity."""
        rot_yz = axis_rotation_matrix("Y", "Z")
        rot_zy = axis_rotation_matrix("Z", "Y")
        v = Gf.Vec3d(1, 2, 3)
        result = rot_zy * (rot_yz * v)
        assert _approx_vec3(result, (1, 2, 3))


# ---------------------------------------------------------------------------
# remap_axis_token
# ---------------------------------------------------------------------------

class TestRemapAxisToken:

    def test_y_to_z_swap(self):
        """Y<->Z is a swap: Y->Z, Z->Y, X stays."""
        assert remap_axis_token("Y", "Y", "Z") == "Z"
        assert remap_axis_token("Z", "Y", "Z") == "Y"
        assert remap_axis_token("X", "Y", "Z") == "X"

    def test_z_to_y_swap(self):
        assert remap_axis_token("Z", "Z", "Y") == "Y"
        assert remap_axis_token("Y", "Z", "Y") == "Z"
        assert remap_axis_token("X", "Z", "Y") == "X"

    def test_same_axis_no_remap(self):
        assert remap_axis_token("Y", "Y", "Y") == "Y"
        assert remap_axis_token("Z", "Z", "Z") == "Z"

    def test_non_axis_token_unchanged(self):
        assert remap_axis_token("horizontal", "Y", "Z") == "horizontal"
        assert remap_axis_token("up", "Y", "Z") == "up"


# ---------------------------------------------------------------------------
# UnitsLens.get_attr with target_up -- Vector rotation
# ---------------------------------------------------------------------------

class TestGetAttrAxisVector:
    """Test vector rotation through get_attr(target_up=...)."""

    def test_velocity_y_to_z(self):
        """physics:velocity (1,2,3) in Y-up -> (1,-3,2) in Z-up.

        Y->Z: (x,y,z) -> (x, -z, y)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert _approx_vec3(result, (1, -3, 2))

    def test_velocity_z_to_y(self):
        """physics:velocity (1,2,3) in Z-up -> (1,3,-2) in Y-up.

        Z->Y: (x,y,z) -> (x, z, -y)
        """
        stage = _make_zup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Y")

        assert _approx_vec3(result, (1, 3, -2))

    def test_translate_y_to_z(self):
        """xformOp:translate (0,5,0) in Y-up -> (0,0,5) in Z-up."""
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)
        attr.Set(Gf.Vec3d(0, 5, 0))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert _approx_vec3(result, (0, 0, 5))

    def test_positions_array_y_to_z(self):
        """Vec3f array of positions rotated from Y-up to Z-up.

        Y->Z: (x,y,z) -> (x, -z, y)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("positions", Sdf.ValueTypeNames.Float3Array)
        attr.Set(Vt.Vec3fArray([
            Gf.Vec3f(1, 0, 0),
            Gf.Vec3f(0, 1, 0),
            Gf.Vec3f(0, 0, 1),
        ]))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert len(result) == 3
        assert _approx_vec3(result[0], (1, 0, 0))   # X unchanged
        assert _approx_vec3(result[1], (0, 0, 1))   # Y -> Z
        assert _approx_vec3(result[2], (0, -1, 0))  # Z -> -Y

    def test_angular_velocity_rotated(self):
        """physics:angularVelocity is VECTOR3 -- rotates under axis change.

        (0,10,0) Y-up -> (0,0,10) Z-up  (angular velocity axis around Y -> around Z)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:angularVelocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(0, 10, 0))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert _approx_vec3(result, (0, 0, 10))


# ---------------------------------------------------------------------------
# UnitsLens.get_attr with target_up -- Scalar passthrough
# ---------------------------------------------------------------------------

class TestGetAttrAxisScalar:
    """Scalars should be unaffected by axis changes."""

    def test_mass_unchanged(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:mass", Sdf.ValueTypeNames.Float)
        attr.Set(10.0)

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert math.isclose(result, 10.0)

    def test_density_unchanged(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:density", Sdf.ValueTypeNames.Float)
        attr.Set(2700.0)

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert math.isclose(result, 2700.0)

    def test_gravity_magnitude_unchanged(self):
        """gravityMagnitude is a scalar -- axis change doesn't affect it."""
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:gravityMagnitude", Sdf.ValueTypeNames.Float)
        attr.Set(9.81)

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert math.isclose(result, 9.81, rel_tol=1e-5)

    def test_size_unchanged(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("size", Sdf.ValueTypeNames.Double)
        attr.Set(2.0)

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert math.isclose(result, 2.0)


# ---------------------------------------------------------------------------
# Same-axis no-op
# ---------------------------------------------------------------------------

class TestSameAxisNoOp:

    def test_y_to_y_no_change(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Y")

        assert _approx_vec3(result, (1, 2, 3))

    def test_z_to_z_no_change(self):
        stage = _make_zup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert _approx_vec3(result, (1, 2, 3))


# ---------------------------------------------------------------------------
# target_up=None backward compat
# ---------------------------------------------------------------------------

class TestNoneAxisBackwardCompat:

    def test_none_target_up_no_rotation(self):
        """target_up=None -> no axis change applied (backward compat)."""
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3))

        result = UnitsLens.get_attr(attr, target_mpu=1.0)  # no target_up

        assert _approx_vec3(result, (1, 2, 3))


# ---------------------------------------------------------------------------
# Combined: unit scaling AND axis change
# ---------------------------------------------------------------------------

class TestCombinedScaleAndAxis:

    def test_cm_yup_to_m_zup(self):
        """Velocity (100, 200, 300) cm/s in Y-up -> m/s in Z-up.

        Step 1: cm->m scaling: x0.01 -> (1, 2, 3)
        Step 2: Y->Z rotation: (x,y,z)->(x,-z,y) -> (1, -3, 2)
        """
        UnitsLens.clear_cache()
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 0.01)
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01, up_axis="Y")

        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(100, 200, 300))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert _approx_vec3(result, (1, -3, 2))

    def test_mm_yup_positions_to_m_zup(self):
        """Positions array in mm Y-up -> meters Z-up."""
        UnitsLens.clear_cache()
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.001, up_axis="Y")

        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("positions", Sdf.ValueTypeNames.Float3Array)
        attr.Set(Vt.Vec3fArray([
            Gf.Vec3f(1000, 0, 0),    # 1m along X
            Gf.Vec3f(0, 2000, 0),    # 2m along Y (up)
        ]))

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert len(result) == 2
        assert _approx_vec3(result[0], (1, 0, 0))    # X preserved, mm->m
        assert _approx_vec3(result[1], (0, 0, 2))    # Y->Z, mm->m


# ---------------------------------------------------------------------------
# set_attr with source_up
# ---------------------------------------------------------------------------

class TestSetAttrAxis:

    def test_set_velocity_from_zup_on_yup_prim(self):
        """Author velocity in Z-up, store on Y-up prim -> rotated to Y-up.

        Value is (1,0,5) in Z-up. Z->Y: (x,y,z)->(x,z,-y) -> (1,5,0) in Y-up.
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)

        UnitsLens.set_attr(attr, Gf.Vec3f(1, 0, 5), source_mpu=1.0, source_up="Z")

        stored = attr.Get()
        assert _approx_vec3(stored, (1, 5, 0))

    def test_set_translate_from_zup_on_yup_prim(self):
        """Translate (0,0,3) Z-up -> stored as (0,3,0) in Y-up prim.

        Z->Y: (x,y,z)->(x,z,-y) -> (0,3,0)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("xformOp:translate", Sdf.ValueTypeNames.Double3)

        UnitsLens.set_attr(attr, Gf.Vec3d(0, 0, 3), source_mpu=1.0, source_up="Z")

        stored = attr.Get()
        assert _approx_vec3(stored, (0, 3, 0))


# ---------------------------------------------------------------------------
# Roundtrip: get(Y->Z) then set(Z->Y) recovers original
# ---------------------------------------------------------------------------

class TestAxisRoundtrip:

    def test_get_set_roundtrip_velocity(self):
        """Read in Z-up, write back from Z-up -> original Y-up value recovered."""
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        original = Gf.Vec3f(1, 2, 3)
        attr.Set(original)

        # Read as Z-up
        in_zup = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        # Write back from Z-up
        UnitsLens.set_attr(attr, in_zup, source_mpu=1.0, source_up="Z")

        stored = attr.Get()
        assert _approx_vec3(stored, original)

    def test_get_set_roundtrip_positions(self):
        """Roundtrip positions array through Z-up conversion."""
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("positions", Sdf.ValueTypeNames.Float3Array)
        original = Vt.Vec3fArray([
            Gf.Vec3f(1, 2, 3),
            Gf.Vec3f(4, 5, 6),
        ])
        attr.Set(original)

        in_zup = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")
        UnitsLens.set_attr(attr, in_zup, source_mpu=1.0, source_up="Z")

        stored = attr.Get()
        assert len(stored) == 2
        assert _approx_vec3(stored[0], original[0])
        assert _approx_vec3(stored[1], original[1])

    def test_combined_roundtrip_cm_yup_to_m_zup(self):
        """Full roundtrip: cm Y-up -> m Z-up -> cm Y-up."""
        UnitsLens.clear_cache()
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        MetricsAPI.apply(root.GetPrim(), meters_per_unit=0.01, up_axis="Y")

        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        original = Gf.Vec3f(100, 200, 300)  # cm/s Y-up
        attr.Set(original)

        # Read as m Z-up
        in_m_zup = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        # Write back from m Z-up
        UnitsLens.set_attr(attr, in_m_zup, source_mpu=1.0, source_up="Z")

        stored = attr.Get()
        assert _approx_vec3(stored, original)


# ---------------------------------------------------------------------------
# Axis token remapping
# ---------------------------------------------------------------------------

class TestAxisTokenRemap:
    """Test axis token remapping directly."""

    def test_y_z_swap(self):
        assert remap_axis_token("Y", "Y", "Z") == "Z"
        assert remap_axis_token("Z", "Y", "Z") == "Y"
        assert remap_axis_token("X", "Y", "Z") == "X"

    def test_z_y_swap(self):
        assert remap_axis_token("Z", "Z", "Y") == "Y"
        assert remap_axis_token("Y", "Z", "Y") == "Z"
        assert remap_axis_token("X", "Z", "Y") == "X"


# ---------------------------------------------------------------------------
# Convenience methods
# ---------------------------------------------------------------------------

class TestConvenienceMethods:

    def test_get_in_zup(self):
        """get_in_zup reads Y-up velocity and converts to Z-up.

        (0,5,0) Y-up -> (0,0,5) Z-up via Y->Z: (x,y,z)->(x,-z,y)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(0, 5, 0))

        result = UnitsLens.get_in_zup(attr)

        assert _approx_vec3(result, (0, 0, 5))

    def test_get_in_yup(self):
        """get_in_yup reads Z-up velocity and converts to Y-up.

        (0,0,5) Z-up -> (0,5,0) Y-up via Z->Y: (x,y,z)->(x,z,-y)
        """
        stage = _make_zup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(0, 0, 5))

        result = UnitsLens.get_in_yup(attr)

        assert _approx_vec3(result, (0, 5, 0))

    def test_get_in_zup_same_axis(self):
        """get_in_zup on already Z-up data -> no change."""
        stage = _make_zup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3))

        result = UnitsLens.get_in_zup(attr)

        assert _approx_vec3(result, (1, 2, 3))


# ---------------------------------------------------------------------------
# get_conversion_info includes axis info
# ---------------------------------------------------------------------------

class TestConversionInfoAxis:

    def test_info_includes_axis_fields(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 0, 0))

        info = UnitsLens.get_conversion_info(attr)

        assert "source_up" in info
        assert info["source_up"] == "Y"
        assert "axis_transform" in info
        assert info["axis_transform"] == AxisTransform.VECTOR3

    def test_info_scalar_attr(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:mass", Sdf.ValueTypeNames.Float)
        attr.Set(10.0)

        info = UnitsLens.get_conversion_info(attr)

        assert info["axis_transform"] == AxisTransform.NONE

    def test_info_unknown_attr(self):
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("custom:thing", Sdf.ValueTypeNames.Float)
        attr.Set(1.0)

        info = UnitsLens.get_conversion_info(attr)

        assert info["axis_transform"] is None  # not in registry


# ---------------------------------------------------------------------------
# Physics stage: realistic scenario
# ---------------------------------------------------------------------------

class TestPhysicsStageAxisChange:
    """Use the physics stage (stage2) which has Y-up and mixed cm/m."""

    def setup_method(self):
        UnitsLens.clear_cache()
        self.stage = build_stage2_physics()

    def test_velocity_m_yup_to_m_zup(self):
        """Robot arm velocity (1,0,0) m/s Y-up -> (1,0,0) m/s Z-up.
        Pure X velocity is unchanged by Y<->Z rotation."""
        arm = self.stage.GetPrimAtPath("/World/Robot/Arm")
        attr = arm.GetAttribute("physics:velocity")

        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        assert _approx_vec3(result, (1, 0, 0))

    def test_gravity_magnitude_unaffected(self):
        """gravityMagnitude is a scalar -- axis change doesn't affect it."""
        scene = self.stage.GetPrimAtPath("/World/PhysicsScene")
        attr = scene.GetAttribute("physics:gravityMagnitude")

        # Read in meters Z-up
        result = UnitsLens.get_attr(attr, target_mpu=1.0, target_up="Z")

        # 981 cm/s2 -> 9.81 m/s2 (unit conversion only, no axis change)
        assert math.isclose(result, 9.81, rel_tol=1e-4)


# ---------------------------------------------------------------------------
# Time samples with axis change
# ---------------------------------------------------------------------------

class TestTimeSamplesAxisChange:

    def test_get_time_samples_with_axis(self):
        """Time-sampled velocity Y-up -> Z-up.

        Y->Z: (x,y,z) -> (x,-z,y)
        (1,2,3) -> (1,-3,2)
        (4,5,6) -> (4,-6,5)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
        attr.Set(Gf.Vec3f(1, 2, 3), 0.0)
        attr.Set(Gf.Vec3f(4, 5, 6), 1.0)

        samples = UnitsLens.get_time_samples(attr, target_mpu=1.0, target_up="Z")

        assert len(samples) == 2
        assert _approx_vec3(samples[0][1], (1, -3, 2))
        assert _approx_vec3(samples[1][1], (4, -6, 5))

    def test_set_time_samples_with_axis(self):
        """Write time-sampled velocity from Z-up into Y-up prim.

        Z->Y: (x,y,z) -> (x,z,-y)
        (1,-3,2) -> (1,2,3)
        (4,-6,5) -> (4,5,6)
        """
        stage = _make_yup_stage()
        prim = stage.GetPrimAtPath("/Root")
        attr = prim.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)

        samples_zup = [
            (0.0, Gf.Vec3f(1, -3, 2)),
            (1.0, Gf.Vec3f(4, -6, 5)),
        ]
        UnitsLens.set_time_samples(attr, samples_zup, source_mpu=1.0, source_up="Z")

        raw0 = attr.Get(0.0)
        raw1 = attr.Get(1.0)
        assert _approx_vec3(raw0, (1, 2, 3))
        assert _approx_vec3(raw1, (4, 5, 6))

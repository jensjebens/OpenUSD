#!/usr/bin/env python3
"""
test_units_resolution.py — TDD tests for unit-aware value resolution.

Modeled on USD's own test patterns (pxr/usd/usdGeom/testenv/testUsdGeomMetrics.py).

Test categories:
  1. Baseline — current USD behavior (no unit correction)
  2. Composition preservation — composition is unaffected
  3. Unit resolution — corrective scaling via our mechanism
  4. Edge cases — nested refs, identity scaling, etc.
"""

import os
import sys
import unittest
import tempfile
import shutil

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(__file__)), 'src'))

from pxr import Usd, UsdGeom, Sdf, Gf, Pcp
from units_resolver import (
    get_meters_per_unit,
    get_up_axis,
    get_kilograms_per_unit,
    get_unit_scale_for_prim,
    get_mass_scale_for_prim,
    get_up_axis_correction_for_prim,
    resolve_translate_in_stage_units,
    resolve_xform_in_stage_units,
    resolve_physics_attr,
    _units_are_close,
    _get_up_axis_rotation,
    PHYSICS_UNIT_DIMENSIONS,
)

TESTENV = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'testenv')


def _make_stage_with_ref(stage_mpu, ref_path, ref_prim_path, prim_name='/Ref'):
    """Helper: create an in-memory stage at stage_mpu that references ref_path."""
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageMetersPerUnit(stage, stage_mpu)
    prim = stage.DefinePrim(prim_name)
    prim.GetReferences().AddReference(ref_path, ref_prim_path)
    return stage, prim


class TestBaseline(unittest.TestCase):
    """Verify current USD behavior — no unit correction, numeric preservation."""

    def test_authored_values_preserved_across_reference(self):
        """Composition must preserve authored numbers exactly."""
        stage, prim = _make_stage_with_ref(
            1.0,  # meters
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        translate = prim.GetAttribute('xformOp:translate').Get()
        # Composition preserves the raw number — no conversion
        self.assertEqual(translate, Gf.Vec3d(100, 200, 300))

    def test_stage_meters_per_unit_is_advisory(self):
        """metersPerUnit is advisory — it does not affect value resolution."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'mm_asset.usda'),
            '/Bolt',
        )
        translate = prim.GetAttribute('xformOp:translate').Get()
        # mm values come through unchanged even in a meter stage
        self.assertEqual(translate, Gf.Vec3d(10000, 5500, 0))

    def test_same_unit_reference_no_change(self):
        """When units match, values are identical (trivially)."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'm_asset.usda'),
            '/World',
        )
        translate = prim.GetAttribute('xformOp:translate').Get()
        self.assertEqual(translate, Gf.Vec3d(0, 0, 0))


class TestCompositionPreservation(unittest.TestCase):
    """Verify that our mechanism does NOT affect composition itself."""

    def test_raw_attribute_unchanged(self):
        """UsdAttribute.Get() must still return authored value, unmodified."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        raw = prim.GetAttribute('xformOp:translate').Get()
        # This is THE critical invariant: composition is unaffected
        self.assertEqual(raw, Gf.Vec3d(100, 200, 300))

    def test_layer_metadata_survives(self):
        """Source layer's metersPerUnit is accessible via PrimStack."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        # We can still find the source layer's metersPerUnit
        prim_index = prim.GetPrimIndex()
        found_cm = False
        for child in prim_index.rootNode.children:
            if child.arcType == Pcp.ArcTypeReference:
                ref_layer = child.layerStack.layers[0]
                ref_mpu = get_meters_per_unit(ref_layer)
                if _units_are_close(ref_mpu, 0.01):
                    found_cm = True
        self.assertTrue(found_cm, "Should find cm metersPerUnit in PrimIndex")

    def test_sdf_layer_values_unchanged(self):
        """Values in the source Sdf layer are never modified."""
        cm_layer = Sdf.Layer.FindOrOpen(os.path.join(TESTENV, 'cm_asset.usda'))
        spec = cm_layer.GetPrimAtPath('/Box')
        # The translate is authored as (100, 200, 300) in the source
        attr_spec = cm_layer.GetAttributeAtPath('/Box.xformOp:translate')
        self.assertIsNotNone(attr_spec)
        self.assertEqual(attr_spec.default, Gf.Vec3d(100, 200, 300))


class TestUnitResolution(unittest.TestCase):
    """Test our unit-aware value resolution mechanism."""

    def test_cm_to_meters_translate(self):
        """100cm translate should resolve to 1m in a meter stage."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(resolved, Gf.Vec3d(1, 2, 3), 1e-6),
            f"Expected (1,2,3) got {resolved}"
        )

    def test_mm_to_meters_translate(self):
        """10000mm translate should resolve to 10m in a meter stage."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'mm_asset.usda'),
            '/Bolt',
        )
        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(resolved, Gf.Vec3d(10, 5.5, 0), 1e-6),
            f"Expected (10, 5.5, 0) got {resolved}"
        )

    def test_same_units_no_scaling(self):
        """When units match, resolved value equals authored value."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'm_asset.usda'),
            '/World',
        )
        resolved = resolve_translate_in_stage_units(prim)
        raw = prim.GetAttribute('xformOp:translate').Get()
        self.assertEqual(resolved, raw)

    def test_cm_to_mm_translate(self):
        """cm asset referenced into mm stage — values should scale up by 10."""
        stage, prim = _make_stage_with_ref(
            0.001,  # mm stage
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNotNone(resolved)
        # 100cm = 1000mm, 200cm = 2000mm, 300cm = 3000mm
        self.assertTrue(
            Gf.IsClose(resolved, Gf.Vec3d(1000, 2000, 3000), 1e-6),
            f"Expected (1000, 2000, 3000) got {resolved}"
        )

    def test_unit_scale_factor(self):
        """get_unit_scale_for_prim returns correct scale factor."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        scale = get_unit_scale_for_prim(prim)
        self.assertAlmostEqual(scale, 0.01, places=6)

    def test_unit_scale_factor_identity(self):
        """Same-unit reference has scale factor 1.0."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'm_asset.usda'),
            '/World',
        )
        scale = get_unit_scale_for_prim(prim)
        self.assertAlmostEqual(scale, 1.0, places=6)

    def test_resolve_xform_matrix(self):
        """Full xform resolution should scale translation component only."""
        stage, prim = _make_stage_with_ref(
            1.0,
            os.path.join(TESTENV, 'cm_asset.usda'),
            '/Box',
        )
        matrix = resolve_xform_in_stage_units(prim)
        translate = matrix.ExtractTranslation()
        self.assertTrue(
            Gf.IsClose(translate, Gf.Vec3d(1, 2, 3), 1e-6),
            f"Expected translation (1,2,3) got {translate}"
        )


class TestEdgeCases(unittest.TestCase):
    """Edge cases and tricky scenarios."""

    def test_no_meters_per_unit_metadata(self):
        """Layer without explicit metersPerUnit defaults to cm (0.01)."""
        layer = Sdf.Layer.CreateAnonymous('.usda')
        # Don't set metersPerUnit — USD default is 0.01 (cm)
        mpu = get_meters_per_unit(layer)
        self.assertAlmostEqual(mpu, 0.01, places=6)

    def test_units_are_close_identical(self):
        self.assertTrue(_units_are_close(1.0, 1.0))

    def test_units_are_close_float_noise(self):
        """12 inches * UsdGeom.LinearUnits.inches ≈ 1 foot but not ==."""
        feet = UsdGeom.LinearUnits.feet
        from_inches = 12 * UsdGeom.LinearUnits.inches
        self.assertTrue(_units_are_close(feet, from_inches))

    def test_units_not_close(self):
        self.assertFalse(_units_are_close(1.0, 0.01))

    def test_nested_references(self):
        """A → B(cm) → C(mm): when A is meters, should we see the full chain?
        For now, we detect the immediate reference mismatch."""
        # Create a cm stage that references the mm asset
        cm_stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(cm_stage, 0.01)
        cm_prim = cm_stage.DefinePrim('/BoltRef')
        cm_prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'mm_asset.usda'), '/Bolt'
        )
        cm_stage.GetRootLayer().Export('/tmp/test_nested_cm.usda')

        # Create a meter stage that references the cm asset
        m_stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(m_stage, 1.0)
        m_prim = m_stage.DefinePrim('/Nested')
        m_prim.GetReferences().AddReference('/tmp/test_nested_cm.usda', '/BoltRef')

        # The raw value should still be the mm-authored value (10000, 5500, 0)
        raw = m_prim.GetAttribute('xformOp:translate').Get()
        self.assertEqual(raw, Gf.Vec3d(10000, 5500, 0))

        # Our resolver should detect the cm layer at the reference boundary
        # and scale by 0.01 (cm→m). The mm→cm mismatch is a separate concern
        # at the inner reference.
        scale = get_unit_scale_for_prim(m_prim)
        # The immediate reference is to the cm layer
        self.assertAlmostEqual(scale, 0.01, places=6)

    def test_prim_without_xform(self):
        """Prim with no translate returns None."""
        stage = Usd.Stage.CreateInMemory()
        stage.GetRootLayer().metersPerUnit = 1.0
        prim = stage.DefinePrim('/Empty')
        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNone(resolved)

    def test_locally_defined_prim_no_reference(self):
        """A prim defined directly on the stage (no reference) has scale 1.0."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)
        xform = UsdGeom.Xform.Define(stage, '/Local')
        xform.AddTranslateOp().Set(Gf.Vec3d(5, 10, 15))
        
        scale = get_unit_scale_for_prim(xform.GetPrim())
        self.assertAlmostEqual(scale, 1.0, places=6)
        
        resolved = resolve_translate_in_stage_units(xform.GetPrim())
        self.assertEqual(resolved, Gf.Vec3d(5, 10, 15))


class TestUpAxis(unittest.TestCase):
    """Test upAxis correction at composition boundaries."""

    def test_zup_asset_into_yup_stage(self):
        """A Z-up asset referenced into a Y-up stage should have
        its vertical axis rotated: (0,0,168) in Z-up → (0,168,0) in Y-up."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 0.01)  # cm, same as asset
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_zup_asset.usda'), '/Robot')

        # Raw value should be unchanged by composition
        raw = prim.GetAttribute('xformOp:translate').Get()
        self.assertEqual(raw, Gf.Vec3d(0, 0, 168))

        # upAxis correction should rotate Z-up to Y-up
        correction = get_up_axis_correction_for_prim(prim)
        self.assertNotEqual(correction, Gf.Matrix4d(1.0))

        # Resolved: (0, 0, 168) Z-up → (0, 168, 0) Y-up
        # No unit scaling (both cm)
        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(resolved, Gf.Vec3d(0, 168, 0), 1e-4),
            f"Expected (0, 168, 0) got {resolved}"
        )

    def test_yup_asset_into_zup_stage(self):
        """A Y-up asset referenced into a Z-up stage should have
        its vertical axis rotated: (100,200,300) in Y-up → (100,−300,200) in Z-up."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 0.01)  # cm
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
        prim = stage.DefinePrim('/Ref')
        # cm_asset.usda is Y-up (default)
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_asset.usda'), '/Box')

        # Resolved: (100,200,300) Y-up → (100,-300,200) Z-up
        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(resolved, Gf.Vec3d(100, -300, 200), 1e-4),
            f"Expected (100, -300, 200) got {resolved}"
        )

    def test_same_up_axis_no_rotation(self):
        """When upAxis matches, no rotation is applied."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'm_zup_asset.usda'), '/Building')

        correction = get_up_axis_correction_for_prim(prim)
        self.assertEqual(correction, Gf.Matrix4d(1.0))

        resolved = resolve_translate_in_stage_units(prim)
        self.assertEqual(resolved, Gf.Vec3d(10, 20, 50))

    def test_combined_upaxis_and_units(self):
        """Both upAxis AND metersPerUnit mismatch: Z-up cm asset into Y-up m stage.
        (0, 0, 168) cm Z-up → rotate to Y-up (0, 168, 0) → scale cm→m (0, 1.68, 0)."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)  # meters
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_zup_asset.usda'), '/Robot')

        resolved = resolve_translate_in_stage_units(prim)
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(resolved, Gf.Vec3d(0, 1.68, 0), 1e-4),
            f"Expected (0, 1.68, 0) got {resolved}"
        )

    def test_up_axis_rotation_matrices(self):
        """Verify the rotation matrices are correct."""
        y_to_z = _get_up_axis_rotation(UsdGeom.Tokens.y, UsdGeom.Tokens.z)
        z_to_y = _get_up_axis_rotation(UsdGeom.Tokens.z, UsdGeom.Tokens.y)

        # Y-up (0,1,0) should become Z-up (0,0,1)
        up_y = y_to_z.TransformDir(Gf.Vec3d(0, 1, 0))
        self.assertTrue(Gf.IsClose(up_y, Gf.Vec3d(0, 0, 1), 1e-6))

        # Z-up (0,0,1) should become Y-up (0,1,0)
        up_z = z_to_y.TransformDir(Gf.Vec3d(0, 0, 1))
        self.assertTrue(Gf.IsClose(up_z, Gf.Vec3d(0, 1, 0), 1e-6))

        # Same-axis should be identity
        same = _get_up_axis_rotation(UsdGeom.Tokens.y, UsdGeom.Tokens.y)
        self.assertEqual(same, Gf.Matrix4d(1.0))

    def test_up_axis_preserves_raw_values(self):
        """Composition still preserves raw values — upAxis correction is our layer."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 0.01)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_zup_asset.usda'), '/Robot')

        raw = prim.GetAttribute('xformOp:translate').Get()
        self.assertEqual(raw, Gf.Vec3d(0, 0, 168),
                         "Composition must preserve authored values")


class TestPhysics(unittest.TestCase):
    """Test physics attribute unit resolution with dimensional analysis."""

    def _make_physics_stage(self, stage_mpu, stage_kpu=1.0, stage_up='Y'):
        """Create a meter/kg stage referencing the cm physics asset."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, stage_mpu)
        UsdGeom.SetStageUpAxis(stage, stage_up)
        from pxr import UsdPhysics
        UsdPhysics.SetStageKilogramsPerUnit(stage, stage_kpu)
        return stage

    def test_gravity_magnitude_cm_to_m(self):
        """Gravity 981 cm/s² in cm asset → 9.81 m/s² in m stage.
        gravityMagnitude has length exponent 1."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/PhysicsScene')

        raw = prim.GetAttribute('physics:gravityMagnitude').Get()
        self.assertAlmostEqual(raw, 981.0, places=1)

        resolved = resolve_physics_attr(prim, 'physics:gravityMagnitude')
        self.assertAlmostEqual(resolved, 9.81, places=2,
            msg=f"Expected 9.81, got {resolved}")

    def test_velocity_cm_to_m(self):
        """Velocity (50, 0, -30) cm/s → (0.5, 0, -0.3) m/s.
        velocity has length exponent 1."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        resolved = resolve_physics_attr(prim, 'physics:velocity')
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(Gf.Vec3d(resolved), Gf.Vec3d(0.5, 0, -0.3), 1e-4),
            f"Expected (0.5, 0, -0.3), got {resolved}")

    def test_density_cm_to_m(self):
        """Density 7.8 g/cm³ → 7800 kg/m³.
        density has length exponent -3, mass exponent 1.
        mpu_scale = 0.01, kpu_scale = 1.0 (both kg).
        scale = 0.01^(-3) * 1.0^1 = 1000000. But wait...
        
        Actually: the cm asset has kgPU=1.0, stage has kgPU=1.0.
        So kpu_scale = 1.0.
        mpu_scale = 0.01 (cm/m).
        density_scale = 0.01^(-3) * 1.0 = 1,000,000.
        7.8 * 1,000,000 = 7,800,000. That's wrong!
        
        The issue: density 7.8 in the cm asset means 7.8 kg/cm³ (since
        kgPU=1), which is 7.8 * 10^6 kg/m³. That IS correct physics
        but it's an unreasonable density for steel.
        
        In practice, the cm asset would have density=0.0078 kg/cm³ for
        steel (7800 kg/m³), or would use different kilogramsPerUnit.
        
        Our resolver is mathematically correct — the issue is in the test
        asset authoring. Let's test with the math, not the physical plausibility.
        """
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        resolved = resolve_physics_attr(prim, 'physics:density')
        # mpu_scale=0.01, length_exp=-3 → 0.01^(-3) = 1e6
        # kpu_scale=1.0 → no mass correction
        # 7.8 * 1e6 = 7,800,000
        self.assertAlmostEqual(resolved, 7.8e6, places=0,
            msg=f"Expected 7.8e6, got {resolved}")

    def test_center_of_mass_cm_to_m(self):
        """centerOfMass (5,5,5) cm → (0.05, 0.05, 0.05) m.
        Length exponent 1."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        resolved = resolve_physics_attr(prim, 'physics:centerOfMass')
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(Gf.Vec3d(resolved), Gf.Vec3d(0.05, 0.05, 0.05), 1e-4),
            f"Expected (0.05, 0.05, 0.05), got {resolved}")

    def test_mass_with_kpu_mismatch(self):
        """Mass from mm/gram asset into m/kg stage.
        mm asset: kgPU=0.001 (grams), mass=50 → 50 grams = 0.05 kg.
        mass has length_exp=0, mass_exp=1.
        kpu_scale = 0.001/1.0 = 0.001.
        50 * 0.001 = 0.05 kg."""
        stage = self._make_physics_stage(1.0, stage_kpu=1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'mm_physics_asset.usda'), '/Bolt')

        resolved = resolve_physics_attr(prim, 'physics:mass')
        self.assertAlmostEqual(resolved, 0.05, places=4,
            msg=f"Expected 0.05 kg, got {resolved}")

    def test_diagonal_inertia_cm_to_m(self):
        """diagonalInertia (100,100,100) in cm/kg → m/kg.
        Has length_exp=2, mass_exp=1. kpu same (both 1.0 kg).
        mpu_scale=0.01, scale = 0.01^2 * 1.0 = 0.0001.
        (100,100,100) * 0.0001 = (0.01, 0.01, 0.01)."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        resolved = resolve_physics_attr(prim, 'physics:diagonalInertia')
        self.assertIsNotNone(resolved)
        self.assertTrue(
            Gf.IsClose(Gf.Vec3d(resolved), Gf.Vec3d(0.01, 0.01, 0.01), 1e-6),
            f"Expected (0.01, 0.01, 0.01), got {resolved}")

    def test_angular_velocity_unchanged(self):
        """angularVelocity is radians/time — no length dimension.
        Should be returned unchanged (not in PHYSICS_UNIT_DIMENSIONS)."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        raw = prim.GetAttribute('physics:angularVelocity').Get()
        resolved = resolve_physics_attr(prim, 'physics:angularVelocity')
        # Not in dimensions table → returned as-is
        self.assertEqual(resolved, raw)

    def test_same_units_no_physics_scaling(self):
        """When all units match, physics values are unchanged."""
        stage = self._make_physics_stage(0.01)  # cm stage, cm asset
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        raw_vel = prim.GetAttribute('physics:velocity').Get()
        resolved_vel = resolve_physics_attr(prim, 'physics:velocity')
        self.assertEqual(resolved_vel, raw_vel)

    def test_gravity_mm_to_m(self):
        """Gravity 9810 mm/s² → 9.81 m/s²."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'mm_physics_asset.usda'), '/PhysicsScene')

        resolved = resolve_physics_attr(prim, 'physics:gravityMagnitude')
        self.assertAlmostEqual(resolved, 9.81, places=2,
            msg=f"Expected 9.81, got {resolved}")

    def test_physics_composition_preserves_values(self):
        """Raw physics attribute values are preserved by composition."""
        stage = self._make_physics_stage(1.0)
        prim = stage.DefinePrim('/Ref')
        prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_physics_asset.usda'), '/Ball')

        raw_vel = prim.GetAttribute('physics:velocity').Get()
        self.assertEqual(raw_vel, Gf.Vec3f(50, 0, -30),
                         "Composition must preserve physics values")

        raw_density = prim.GetAttribute('physics:density').Get()
        self.assertAlmostEqual(raw_density, 7.8, places=1,
                               msg="Composition must preserve density")


if __name__ == "__main__":
    unittest.main()

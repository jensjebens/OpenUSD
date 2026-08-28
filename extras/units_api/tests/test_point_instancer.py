"""Tests for PointInstancer unit conversion."""

import pytest
from pxr import Gf, Vt

from units_api import UnitsLens
from test_stages import build_stage6_point_instancer


class TestPointInstancer:
    """PointInstancer attributes — positions, velocities, accelerations, orientations."""

    def setup_method(self):
        self.stage = build_stage6_point_instancer()
        self.pi_prim = self.stage.GetPrimAtPath("/Forest/Trees")

    # --- Positions (L¹) ---

    def test_positions_cm_to_m(self):
        """positions in cm → read in meters."""
        attr = self.pi_prim.GetAttribute("positions")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert len(result) == 3
        # Tree 0: (100, 0, 0) cm → (1, 0, 0) m
        assert result[0][0] == pytest.approx(1.0)
        assert result[0][1] == pytest.approx(0.0)
        assert result[0][2] == pytest.approx(0.0)
        # Tree 1: (500, 0, 200) cm → (5, 0, 2) m
        assert result[1][0] == pytest.approx(5.0)
        assert result[1][2] == pytest.approx(2.0)
        # Tree 2: (1000, 0, -300) cm → (10, 0, -3) m
        assert result[2][0] == pytest.approx(10.0)
        assert result[2][2] == pytest.approx(-3.0)

    def test_positions_cm_to_mm(self):
        """positions in cm → read in mm."""
        attr = self.pi_prim.GetAttribute("positions")
        result = UnitsLens.get_attr(attr, target_mpu=0.001)

        # Tree 0: (100, 0, 0) cm → (1000, 0, 0) mm
        assert result[0][0] == pytest.approx(1000.0)

    def test_positions_same_units(self):
        """positions in cm → read in cm → unchanged."""
        attr = self.pi_prim.GetAttribute("positions")
        result = UnitsLens.get_attr(attr, target_mpu=0.01)

        assert result[0][0] == pytest.approx(100.0)

    # --- Velocities (L¹·T⁻¹) ---

    def test_velocities_cm_to_m(self):
        """velocities in cm/s → read in m/s."""
        attr = self.pi_prim.GetAttribute("velocities")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        # Tree 0: (5, 0, 0) cm/s → (0.05, 0, 0) m/s
        assert result[0][0] == pytest.approx(0.05)

    def test_velocities_cm_to_mm(self):
        """velocities in cm/s → read in mm/s."""
        attr = self.pi_prim.GetAttribute("velocities")
        result = UnitsLens.get_attr(attr, target_mpu=0.001)

        # Tree 0: (5, 0, 0) cm/s → (50, 0, 0) mm/s
        assert result[0][0] == pytest.approx(50.0)

    # --- Accelerations (L¹·T⁻²) ---

    def test_accelerations_cm_to_m(self):
        """accelerations in cm/s² → read in m/s²."""
        attr = self.pi_prim.GetAttribute("accelerations")
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        # (0, -981, 0) cm/s² → (0, -9.81, 0) m/s²
        assert result[0][1] == pytest.approx(-9.81)

    # --- Orientations (unitless) ---

    def test_orientations_unchanged(self):
        """orientations are unitless — must NOT be converted."""
        attr = self.pi_prim.GetAttribute("orientations")
        raw = attr.Get()
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        # Should be identical — no conversion
        assert len(result) == 3
        for i in range(3):
            assert result[i] == raw[i]

    # --- Authoring ---

    def test_set_positions_m_on_cm_stage(self):
        """Author positions in meters on a cm-stage PointInstancer."""
        attr = self.pi_prim.GetAttribute("positions")
        new_positions = Vt.Vec3fArray([
            Gf.Vec3f(2.0, 0, 0),    # 2 m
            Gf.Vec3f(7.5, 0, 1.0),  # 7.5 m, 1 m
        ])
        UnitsLens.set_attr(attr, new_positions, source_mpu=1.0)

        # Read raw — should be in cm
        raw = attr.Get()
        assert raw[0][0] == pytest.approx(200.0)   # 2 m → 200 cm
        assert raw[1][0] == pytest.approx(750.0)   # 7.5 m → 750 cm
        assert raw[1][2] == pytest.approx(100.0)   # 1 m → 100 cm

    def test_set_get_positions_roundtrip(self):
        """Set positions in meters, read back in meters → same values."""
        attr = self.pi_prim.GetAttribute("positions")
        original = Vt.Vec3fArray([
            Gf.Vec3f(3.0, 0, 0),
            Gf.Vec3f(0, 5.0, 0),
        ])
        UnitsLens.set_attr(attr, original, source_mpu=1.0)
        result = UnitsLens.get_attr(attr, target_mpu=1.0)

        assert result[0][0] == pytest.approx(3.0, abs=1e-5)
        assert result[1][1] == pytest.approx(5.0, abs=1e-5)

    # --- Conversion info ---

    def test_conversion_info_positions(self):
        """Positions should use registry (not per-attribute)."""
        attr = self.pi_prim.GetAttribute("positions")
        info = UnitsLens.get_conversion_info(attr)
        assert info["unit_source"] == "registry"
        assert info["dimension"].L == 1
        assert info["source_mpu"] == pytest.approx(0.01)

    def test_conversion_info_orientations(self):
        """Orientations are unitless — dimension should be (0,0,0)."""
        attr = self.pi_prim.GetAttribute("orientations")
        info = UnitsLens.get_conversion_info(attr)
        assert info["dimension"].L == 0
        assert info["dimension"].M == 0
        assert info["dimension"].T == 0

"""Tests for dimensional registry and conversion factor calculation."""

import pytest
from units_api import Dimension, get_dimension, conversion_factor


# ---------------------------------------------------------------------------
# Registry lookups
# ---------------------------------------------------------------------------

def test_dimension_registry_known():
    """Known attributes return correct dimensional exponents."""
    assert get_dimension("xformOp:translate") == Dimension(L=1, M=0, T=0)
    assert get_dimension("points") == Dimension(L=1, M=0, T=0)
    assert get_dimension("extent") == Dimension(L=1, M=0, T=0)
    assert get_dimension("size") == Dimension(L=1, M=0, T=0)
    assert get_dimension("focusDistance") == Dimension(L=1, M=0, T=0)
    assert get_dimension("clippingRange") == Dimension(L=1, M=0, T=0)
    assert get_dimension("inputs:width") == Dimension(L=1, M=0, T=0)
    assert get_dimension("inputs:height") == Dimension(L=1, M=0, T=0)
    assert get_dimension("physics:mass") == Dimension(L=0, M=1, T=0)
    assert get_dimension("physics:density") == Dimension(L=-3, M=1, T=0)
    assert get_dimension("physics:velocity") == Dimension(L=1, M=0, T=-1)
    assert get_dimension("physics:gravityMagnitude") == Dimension(L=1, M=0, T=-2)


def test_dimension_registry_unitless():
    """Unitless attributes return Dimension(0,0,0)."""
    assert get_dimension("xformOp:scale") == Dimension(L=0, M=0, T=0)
    assert get_dimension("visibility") == Dimension(L=0, M=0, T=0)
    assert get_dimension("doubleSided") == Dimension(L=0, M=0, T=0)


def test_dimension_registry_unknown():
    """Unknown attribute returns None."""
    assert get_dimension("myPipeline:innerRadius") is None
    assert get_dimension("nonexistent:attr") is None
    assert get_dimension("focalLength") is None          # fixed mm, not in registry
    assert get_dimension("horizontalAperture") is None   # fixed mm, not in registry


# ---------------------------------------------------------------------------
# Conversion factors
# ---------------------------------------------------------------------------

def test_conversion_factor_linear_cm_to_m():
    """Length attribute: cm → m gives factor = 0.01."""
    dim = Dimension(L=1)
    # source = 0.01 m/unit (cm), target = 1.0 m/unit (m)
    factor = conversion_factor(source_mpu=0.01, target_mpu=1.0, dimension=dim)
    assert factor == pytest.approx(0.01)


def test_conversion_factor_linear_m_to_cm():
    """Length attribute: m → cm gives factor = 100."""
    dim = Dimension(L=1)
    factor = conversion_factor(source_mpu=1.0, target_mpu=0.01, dimension=dim)
    assert factor == pytest.approx(100.0)


def test_conversion_factor_linear_mm_to_m():
    """Length attribute: mm → m gives factor = 0.001."""
    dim = Dimension(L=1)
    factor = conversion_factor(source_mpu=0.001, target_mpu=1.0, dimension=dim)
    assert factor == pytest.approx(0.001)


def test_conversion_factor_density():
    """Density (L=-3, M=1) scales as L⁻³·M¹.

    2700 kg/m³ expressed in a cm-unit system:
    source = 1.0 (m), target = 0.01 (cm)
    factor = (1.0/0.01)^-3 * (1.0/1.0)^1 = 100^-3 = 1e-6
    2700 * 1e-6 = 0.0027 kg/cm³  ✓
    """
    dim = Dimension(L=-3, M=1)
    factor = conversion_factor(
        source_mpu=1.0, target_mpu=0.01,
        dimension=dim,
        source_kpu=1.0, target_kpu=1.0,
    )
    assert factor == pytest.approx(1e-6)


def test_conversion_factor_gravity():
    """Gravity (L=1, T=-2) — cm/s² to m/s² gives factor = 0.01 (only L matters)."""
    dim = Dimension(L=1, T=-2)
    # 981 cm/s² → m/s²: 981 * 0.01 = 9.81
    factor = conversion_factor(source_mpu=0.01, target_mpu=1.0, dimension=dim)
    assert factor == pytest.approx(0.01)


def test_conversion_factor_unitless():
    """Unitless attributes always have factor = 1.0."""
    dim = Dimension(L=0, M=0, T=0)
    factor = conversion_factor(source_mpu=0.01, target_mpu=1.0, dimension=dim)
    assert factor == pytest.approx(1.0)

    factor2 = conversion_factor(source_mpu=0.001, target_mpu=100.0, dimension=dim)
    assert factor2 == pytest.approx(1.0)


def test_conversion_factor_same_units():
    """Same source and target units always give factor = 1.0."""
    for dim in [Dimension(L=1), Dimension(L=-3, M=1), Dimension(L=1, T=-2)]:
        assert conversion_factor(0.01, 0.01, dim) == pytest.approx(1.0)
        assert conversion_factor(1.0, 1.0, dim) == pytest.approx(1.0)


def test_conversion_factor_mass_only():
    """Mass attribute (M=1) scales with kpu, not mpu."""
    dim = Dimension(M=1)
    # source in grams (0.001 kg/unit), target in kg (1.0 kg/unit)
    factor = conversion_factor(
        source_mpu=1.0, target_mpu=1.0,
        dimension=dim,
        source_kpu=0.001, target_kpu=1.0,
    )
    assert factor == pytest.approx(0.001)

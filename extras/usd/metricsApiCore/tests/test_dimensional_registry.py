"""Symmetric tests for DimensionalRegistry (C++ bindings).

These tests mirror extras/units_api/tests/test_dimensions.py exactly.
Same test names, same assertions, different implementation under test.

Requires: USD built with metricsApiCore (extras/usd/metricsApiCore).
"""

import pytest

# Import from C++ bindings — skip entire module if not built
UsdMetricsApi = pytest.importorskip("pxr.UsdMetricsApi")


# ---------------------------------------------------------------------------
# Registry lookups — mirrors test_dimension_registry_known
# ---------------------------------------------------------------------------

def test_dimension_registry_known():
    """Known attributes return correct dimensional exponents."""
    reg = UsdMetricsApi.DimensionalRegistry.GetInstance()

    d = reg.GetDimension("xformOp:translate")
    assert d is not None and d["L"] == 1 and d["M"] == 0 and d["T"] == 0

    d = reg.GetDimension("points")
    assert d is not None and d["L"] == 1

    d = reg.GetDimension("focusDistance")
    assert d is not None and d["L"] == 1

    d = reg.GetDimension("physics:mass")
    assert d is not None and d["L"] == 0 and d["M"] == 1 and d["T"] == 0

    d = reg.GetDimension("physics:density")
    assert d is not None and d["L"] == -3 and d["M"] == 1 and d["T"] == 0

    d = reg.GetDimension("physics:velocity")
    assert d is not None and d["L"] == 1 and d["M"] == 0 and d["T"] == -1

    d = reg.GetDimension("physics:gravityMagnitude")
    assert d is not None and d["L"] == 1 and d["M"] == 0 and d["T"] == -2


def test_dimension_registry_unitless():
    """Unitless attributes return {L:0, M:0, T:0}."""
    reg = UsdMetricsApi.DimensionalRegistry.GetInstance()

    d = reg.GetDimension("xformOp:scale")
    assert d is not None and d["L"] == 0 and d["M"] == 0 and d["T"] == 0

    d = reg.GetDimension("orientations")
    assert d is not None and d["L"] == 0 and d["M"] == 0 and d["T"] == 0


def test_dimension_registry_unknown():
    """Unknown attribute returns None."""
    reg = UsdMetricsApi.DimensionalRegistry.GetInstance()

    assert reg.GetDimension("myPipeline:innerRadius") is None
    assert reg.GetDimension("nonexistent:attr") is None
    assert reg.GetDimension("focalLength") is None
    assert reg.GetDimension("horizontalAperture") is None


def test_has_dimension():
    """HasDimension returns bool."""
    reg = UsdMetricsApi.DimensionalRegistry.GetInstance()

    assert reg.HasDimension("xformOp:translate") is True
    assert reg.HasDimension("physics:density") is True
    assert reg.HasDimension("focalLength") is False
    assert reg.HasDimension("nonexistent") is False


# ---------------------------------------------------------------------------
# Conversion factors — mirrors test_conversion_factor_*
# ---------------------------------------------------------------------------

def test_conversion_factor_linear_cm_to_m():
    """Length attribute: cm → m gives factor = 0.01."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=1, M=0, T=0, sourceMpu=0.01, targetMpu=1.0)
    assert factor == pytest.approx(0.01)


def test_conversion_factor_linear_m_to_cm():
    """Length attribute: m → cm gives factor = 100."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=1, M=0, T=0, sourceMpu=1.0, targetMpu=0.01)
    assert factor == pytest.approx(100.0)


def test_conversion_factor_linear_mm_to_m():
    """Length attribute: mm → m gives factor = 0.001."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=1, M=0, T=0, sourceMpu=0.001, targetMpu=1.0)
    assert factor == pytest.approx(0.001)


def test_conversion_factor_density():
    """Density (L=-3, M=1): m → cm gives factor = 1e-6."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=-3, M=1, T=0,
        sourceMpu=1.0, targetMpu=0.01,
        sourceKpu=1.0, targetKpu=1.0)
    assert factor == pytest.approx(1e-6)


def test_conversion_factor_gravity():
    """Gravity (L=1, T=-2): cm → m gives factor = 0.01."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=1, M=0, T=-2, sourceMpu=0.01, targetMpu=1.0)
    assert factor == pytest.approx(0.01)


def test_conversion_factor_unitless():
    """Unitless attributes always have factor = 1.0."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=0, M=0, T=0, sourceMpu=0.01, targetMpu=1.0)
    assert factor == pytest.approx(1.0)

    factor2 = UsdMetricsApi.ComputeConversionFactor(
        L=0, M=0, T=0, sourceMpu=0.001, targetMpu=100.0)
    assert factor2 == pytest.approx(1.0)


def test_conversion_factor_same_units():
    """Same source and target units always give factor = 1.0."""
    for L, M, T in [(1, 0, 0), (-3, 1, 0), (1, 0, -2)]:
        f1 = UsdMetricsApi.ComputeConversionFactor(
            L=L, M=M, T=T, sourceMpu=0.01, targetMpu=0.01)
        assert f1 == pytest.approx(1.0)

        f2 = UsdMetricsApi.ComputeConversionFactor(
            L=L, M=M, T=T, sourceMpu=1.0, targetMpu=1.0)
        assert f2 == pytest.approx(1.0)


def test_conversion_factor_mass_only():
    """Mass attribute (M=1) scales with kpu, not mpu."""
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=0, M=1, T=0,
        sourceMpu=1.0, targetMpu=1.0,
        sourceKpu=0.001, targetKpu=1.0)
    assert factor == pytest.approx(0.001)

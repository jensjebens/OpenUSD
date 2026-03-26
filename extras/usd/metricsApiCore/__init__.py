"""USD Metrics API — prim-level unit declarations and dimensional registry.

Usage:
    from pxr import UsdMetricsApi

    # Apply metrics to a prim
    api = UsdMetricsApi.GeomMetricsAPI.Apply(prim)
    api.CreateMetersPerUnitAttr().Set(0.001)  # millimeters

    # Resolve effective metrics via ancestor walk
    mpu = UsdMetricsApi.GetEffectiveMetersPerUnit(prim)

    # Look up dimensional exponents
    registry = UsdMetricsApi.DimensionalRegistry.GetInstance()
    dim = registry.GetDimension("physics:density")  # {"L": -3, "M": 1, "T": 0}

    # Compute conversion factor
    factor = UsdMetricsApi.ComputeConversionFactor(
        L=-3, M=1, T=0,
        sourceMpu=0.001, targetMpu=1.0)
"""

from pxr import Plug

# Ensure this plugin is registered for plugInfo.json discovery.
Plug.Registry().GetAllPlugins()

try:
    from . import _usdMetricsApi
    from ._usdMetricsApi import *
except ImportError:
    # Not yet built — C++ bindings haven't been compiled
    pass

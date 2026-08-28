/// \file usdMetricsApi/wrapMetricsUtils.cpp
///
/// Python bindings for metrics resolution utility functions.

#include "metricsUtils.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/external/boost/python.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

void
wrapMetricsUtils()
{
    def("GetEffectiveMetersPerUnit",
        UsdMetricsGetEffectiveMetersPerUnit,
        arg("prim"),
        "Get effective metersPerUnit for a prim by walking ancestors.\n"
        "Falls back to stage metadata, then USD default (0.01).");

    def("GetEffectiveUpAxis",
        UsdMetricsGetEffectiveUpAxis,
        arg("prim"),
        "Get effective upAxis for a prim by walking ancestors.\n"
        "Falls back to stage metadata, then USD default (Y).");

    def("GetEffectiveKilogramsPerUnit",
        UsdMetricsGetEffectiveKilogramsPerUnit,
        arg("prim"),
        "Get effective kilogramsPerUnit for a prim by walking ancestors.\n"
        "Falls back to stage metadata, then USD default (1.0).");
}

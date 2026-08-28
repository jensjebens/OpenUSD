/// \file usdMetricsApi/wrapDimensionalRegistry.cpp
///
/// Python bindings for UsdMetricsDimensionalRegistry and UsdMetricsDimension.

#include "dimensionalRegistry.h"

#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/external/boost/python.hpp"
#include "pxr/external/boost/python/dict.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

// Return dimension as a Python dict {"L": int, "M": int, "T": int}
// or None if not found.
static object
_GetDimension(const UsdMetricsDimensionalRegistry& self,
              const std::string& attrName)
{
    auto dim = self.GetDimension(attrName);
    if (!dim) {
        return object();  // None
    }
    dict d;
    d["L"] = dim->L;
    d["M"] = dim->M;
    d["T"] = dim->T;
    return d;
}

// Return all dimensions as a dict of {str: {"L": int, "M": int, "T": int}}
static dict
_GetAllDimensions(const UsdMetricsDimensionalRegistry& self)
{
    dict result;
    for (const auto& entry : self.GetAllDimensions()) {
        dict d;
        d["L"] = entry.second.L;
        d["M"] = entry.second.M;
        d["T"] = entry.second.T;
        result[entry.first] = d;
    }
    return result;
}

static double
_ComputeConversionFactor(int L, int M, int T,
                         double sourceMpu, double targetMpu,
                         double sourceKpu, double targetKpu)
{
    UsdMetricsDimension dim{L, M, T};
    return dim.ComputeConversionFactor(sourceMpu, targetMpu, sourceKpu, targetKpu);
}

} // anonymous namespace

void
wrapDimensionalRegistry()
{
    class_<UsdMetricsDimensionalRegistry, noncopyable>(
        "DimensionalRegistry",
        "Singleton registry of attribute name -> dimensional exponents.\n"
        "Populated from plugInfo.json DimensionalExponents entries.",
        no_init)
        .def("GetInstance",
             &UsdMetricsDimensionalRegistry::GetInstance,
             return_value_policy<reference_existing_object>())
        .staticmethod("GetInstance")
        .def("GetDimension", _GetDimension,
             arg("attrName"),
             "Look up dimensional exponents for an attribute name.\n"
             "Returns dict with L, M, T keys, or None if not found.")
        .def("HasDimension",
             &UsdMetricsDimensionalRegistry::HasDimension,
             arg("attrName"),
             "Check if an attribute is in the registry.")
        .def("GetAllDimensions", _GetAllDimensions,
             "Get all registered entries as {name: {L, M, T}}.")
    ;

    def("ComputeConversionFactor", _ComputeConversionFactor,
        (arg("L"), arg("M"), arg("T"),
         arg("sourceMpu"), arg("targetMpu"),
         arg("sourceKpu") = 1.0, arg("targetKpu") = 1.0),
        "Compute conversion factor for given dimensional exponents.\n"
        "factor = (sourceMpu/targetMpu)^L * (sourceKpu/targetKpu)^M");
}

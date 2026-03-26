#ifndef USD_METRICS_DIMENSIONAL_REGISTRY_H
#define USD_METRICS_DIMENSIONAL_REGISTRY_H

/// \file usdMetricsApi/dimensionalRegistry.h
///
/// Dimensional exponent registry — maps attribute names to L/M/T exponents.
///
/// Exponents are loaded from plugInfo.json "DimensionalExponents" entries
/// across all registered plugins. Each schema domain contributes its own
/// entries. Third-party schemas register additional entries via their own
/// plugInfo.json using the same DimensionalExponents key.
///
/// Usage:
///   auto dim = UsdMetricsDimensionalRegistry::GetInstance().GetDimension("physics:density");
///   if (dim) {
///       // dim->L == -3, dim->M == 1, dim->T == 0
///       double factor = std::pow(sourceMpu / targetMpu, dim->L)
///                     * std::pow(sourceKpu / targetKpu, dim->M);
///   }

#include "pxr/pxr.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/token.h"

#include <optional>
#include <string>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// Dimensional exponents for a physical quantity.
struct UsdMetricsDimension {
    int L = 0;  ///< Length exponent
    int M = 0;  ///< Mass exponent
    int T = 0;  ///< Time exponent

    bool IsUnitless() const { return L == 0 && M == 0 && T == 0; }

    /// Compute the conversion factor for these exponents.
    /// factor = (sourceMpu/targetMpu)^L * (sourceKpu/targetKpu)^M
    /// Time exponents are not converted (no secondsPerUnit concept).
    double ComputeConversionFactor(
        double sourceMpu, double targetMpu,
        double sourceKpu = 1.0, double targetKpu = 1.0) const;
};

/// Singleton registry of attribute name → dimensional exponents.
///
/// Populated from plugInfo.json "DimensionalExponents" entries across all
/// registered plugins at first access. Thread-safe after initialization.
class UsdMetricsDimensionalRegistry {
public:
    /// Get the singleton instance.
    static UsdMetricsDimensionalRegistry& GetInstance();

    /// Look up dimensional exponents for an attribute name.
    /// Returns std::nullopt if the attribute is not in the registry
    /// (unknown attribute — not the same as unitless, which returns
    /// a Dimension with all-zero exponents).
    std::optional<UsdMetricsDimension> GetDimension(
        const std::string& attrName) const;

    /// Check if an attribute is registered.
    bool HasDimension(const std::string& attrName) const;

    /// Get all registered entries (for debugging/introspection).
    const std::unordered_map<std::string, UsdMetricsDimension>& GetAllDimensions() const;

private:
    UsdMetricsDimensionalRegistry();
    void _LoadFromPlugins();

    std::unordered_map<std::string, UsdMetricsDimension> _dimensions;

    friend class TfSingleton<UsdMetricsDimensionalRegistry>;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_METRICS_DIMENSIONAL_REGISTRY_H

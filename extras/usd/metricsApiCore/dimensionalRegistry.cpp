/// \file usdMetricsApi/dimensionalRegistry.cpp

#include "dimensionalRegistry.h"

#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/instantiateSingleton.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(UsdMetricsDimensionalRegistry);

double
UsdMetricsDimension::ComputeConversionFactor(
    double sourceMpu, double targetMpu,
    double sourceKpu, double targetKpu) const
{
    double factor = 1.0;
    if (L != 0) {
        factor *= std::pow(sourceMpu / targetMpu, L);
    }
    if (M != 0) {
        factor *= std::pow(sourceKpu / targetKpu, M);
    }
    // T exponent: no timePerUnit concept, factor = 1
    return factor;
}

UsdMetricsDimensionalRegistry&
UsdMetricsDimensionalRegistry::GetInstance()
{
    return TfSingleton<UsdMetricsDimensionalRegistry>::GetInstance();
}

UsdMetricsDimensionalRegistry::UsdMetricsDimensionalRegistry()
{
    _LoadFromPlugins();
}

void
UsdMetricsDimensionalRegistry::_LoadFromPlugins()
{
    // Discover all plugins and collect DimensionalExponents entries
    PlugPluginPtrVector plugins = PlugRegistry::GetInstance().GetAllPlugins();

    for (const auto& plugin : plugins) {
        JsObject metadata = plugin->GetMetadata();
        auto it = metadata.find("DimensionalExponents");
        if (it == metadata.end()) {
            continue;
        }

        if (!it->second.IsObject()) {
            continue;
        }

        const JsObject& exponents = it->second.GetJsObject();
        for (const auto& entry : exponents) {
            const std::string& attrName = entry.first;

            // Skip documentation/comment keys
            if (attrName.substr(0, 2) == "__") {
                continue;
            }

            if (!entry.second.IsObject()) {
                // Empty object → unitless
                _dimensions[attrName] = UsdMetricsDimension{0, 0, 0};
                continue;
            }

            const JsObject& dimObj = entry.second.GetJsObject();
            UsdMetricsDimension dim;

            auto lIt = dimObj.find("L");
            if (lIt != dimObj.end() && lIt->second.IsInt()) {
                dim.L = lIt->second.GetInt();
            }

            auto mIt = dimObj.find("M");
            if (mIt != dimObj.end() && mIt->second.IsInt()) {
                dim.M = mIt->second.GetInt();
            }

            auto tIt = dimObj.find("T");
            if (tIt != dimObj.end() && tIt->second.IsInt()) {
                dim.T = tIt->second.GetInt();
            }

            _dimensions[attrName] = dim;
        }
    }
}

std::optional<UsdMetricsDimension>
UsdMetricsDimensionalRegistry::GetDimension(const std::string& attrName) const
{
    auto it = _dimensions.find(attrName);
    if (it != _dimensions.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool
UsdMetricsDimensionalRegistry::HasDimension(const std::string& attrName) const
{
    return _dimensions.count(attrName) > 0;
}

const std::unordered_map<std::string, UsdMetricsDimension>&
UsdMetricsDimensionalRegistry::GetAllDimensions() const
{
    return _dimensions;
}

PXR_NAMESPACE_CLOSE_SCOPE

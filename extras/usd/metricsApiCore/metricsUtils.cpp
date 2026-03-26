/// \file usdMetricsApi/metricsUtils.cpp

#include "metricsUtils.h"
#include "geomMetricsAPI.h"
#include "physicsMetricsAPI.h"

#include "pxr/usd/usdGeom/metrics.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Default values matching USD conventions
constexpr double _DEFAULT_METERS_PER_UNIT = 0.01;  // centimeters
constexpr double _DEFAULT_KILOGRAMS_PER_UNIT = 1.0;
const TfToken _DEFAULT_UP_AXIS("Y");

} // anonymous namespace

double
UsdMetricsGetEffectiveMetersPerUnit(const UsdPrim& prim)
{
    // Walk the prim and its ancestors
    UsdPrim current = prim;
    while (current.IsValid()) {
        if (UsdMetricsGeomMetricsAPI api = UsdMetricsGeomMetricsAPI(current)) {
            if (api.GetMetersPerUnitAttr().HasAuthoredValue()) {
                double mpu;
                if (api.GetMetersPerUnitAttr().Get(&mpu)) {
                    return mpu;
                }
            }
        }
        current = current.GetParent();
    }

    // Fall back to stage-level metadata
    if (UsdStagePtr stage = prim.GetStage()) {
        double stageMpu = UsdGeomGetStageMetersPerUnit(stage);
        if (stageMpu > 0) {
            return stageMpu;
        }
    }

    return _DEFAULT_METERS_PER_UNIT;
}

TfToken
UsdMetricsGetEffectiveUpAxis(const UsdPrim& prim)
{
    UsdPrim current = prim;
    while (current.IsValid()) {
        if (UsdMetricsGeomMetricsAPI api = UsdMetricsGeomMetricsAPI(current)) {
            if (api.GetUpAxisAttr().HasAuthoredValue()) {
                TfToken upAxis;
                if (api.GetUpAxisAttr().Get(&upAxis)) {
                    return upAxis;
                }
            }
        }
        current = current.GetParent();
    }

    // Fall back to stage-level metadata
    if (UsdStagePtr stage = prim.GetStage()) {
        TfToken stageUp = UsdGeomGetStageUpAxis(stage);
        if (!stageUp.IsEmpty()) {
            return stageUp;
        }
    }

    return _DEFAULT_UP_AXIS;
}

double
UsdMetricsGetEffectiveKilogramsPerUnit(const UsdPrim& prim)
{
    UsdPrim current = prim;
    while (current.IsValid()) {
        if (UsdMetricsPhysicsMetricsAPI api = UsdMetricsPhysicsMetricsAPI(current)) {
            if (api.GetKilogramsPerUnitAttr().HasAuthoredValue()) {
                double kpu;
                if (api.GetKilogramsPerUnitAttr().Get(&kpu)) {
                    return kpu;
                }
            }
        }
        current = current.GetParent();
    }

    // Fall back to stage-level metadata (UsdPhysics)
    // UsdPhysics may not be available in all builds, so use a safe approach
    if (UsdStagePtr stage = prim.GetStage()) {
        VtValue val;
        if (stage->GetMetadata(TfToken("kilogramsPerUnit"), &val)) {
            if (val.IsHolding<double>()) {
                double kpu = val.UncheckedGet<double>();
                if (kpu > 0) {
                    return kpu;
                }
            }
        }
    }

    return _DEFAULT_KILOGRAMS_PER_UNIT;
}

PXR_NAMESPACE_CLOSE_SCOPE

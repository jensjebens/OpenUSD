#ifndef USD_METRICS_UTILS_H
#define USD_METRICS_UTILS_H

/// \file usdMetricsApi/metricsUtils.h
///
/// Utility functions for resolving effective unit metrics via ancestor walk.
///
/// These functions implement the inheritance behavior described in the
/// MetricsAPI proposal: walk up the ancestor chain to find the nearest
/// prim with GeomMetricsAPI/PhysicsMetricsAPI applied, falling back to
/// stage-level layer metadata, then to USD defaults.

#include "pxr/pxr.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "api.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Get the effective metersPerUnit for a prim by walking ancestors.
///
/// Search order:
///   1. The prim itself (if GeomMetricsAPI is applied and metersPerUnit is authored)
///   2. Each ancestor up to the pseudo-root
///   3. Stage-level metersPerUnit metadata
///   4. USD default (0.01 = centimeters)
///
/// \param prim The prim to query.
/// \return The effective metersPerUnit value.
USDMETRICSAPI_API
double UsdMetricsGetEffectiveMetersPerUnit(const UsdPrim& prim);

/// Get the effective upAxis for a prim by walking ancestors.
///
/// Same search order as UsdMetricsGetEffectiveMetersPerUnit.
///
/// \param prim The prim to query.
/// \return The effective upAxis token ("Y" or "Z").
USDMETRICSAPI_API
TfToken UsdMetricsGetEffectiveUpAxis(const UsdPrim& prim);

/// Get the effective kilogramsPerUnit for a prim by walking ancestors.
///
/// Same search order, using PhysicsMetricsAPI.
///
/// \param prim The prim to query.
/// \return The effective kilogramsPerUnit value.
USDMETRICSAPI_API
double UsdMetricsGetEffectiveKilogramsPerUnit(const UsdPrim& prim);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_METRICS_UTILS_H

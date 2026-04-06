//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdMetricsTokensType::UsdMetricsTokensType() :
    inherited("inherited", TfToken::Immortal),
    metricsKilogramsPerUnit("metrics:kilogramsPerUnit", TfToken::Immortal),
    metricsMetersPerUnit("metrics:metersPerUnit", TfToken::Immortal),
    metricsUpAxis("metrics:upAxis", TfToken::Immortal),
    Y("Y", TfToken::Immortal),
    Z("Z", TfToken::Immortal),
    GeomMetricsAPI("GeomMetricsAPI", TfToken::Immortal),
    PhysicsMetricsAPI("PhysicsMetricsAPI", TfToken::Immortal),
    allTokens({
        inherited,
        metricsKilogramsPerUnit,
        metricsMetersPerUnit,
        metricsUpAxis,
        Y,
        Z,
        GeomMetricsAPI,
        PhysicsMetricsAPI
    })
{
}

TfStaticData<UsdMetricsTokensType> UsdMetricsTokens;

PXR_NAMESPACE_CLOSE_SCOPE

//
// Copyright 2026 NVIDIA Corporation
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/base/tf/pyModule.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_WRAP_MODULE
{
    // Schema-generated wraps
    TF_WRAP(UsdMetricsGeomMetricsAPI);
    TF_WRAP(UsdMetricsPhysicsMetricsAPI);
    TF_WRAP(UsdMetricsApiTokens);

    // Custom wraps
    TF_WRAP(DimensionalRegistry);
    TF_WRAP(MetricsUtils);
}

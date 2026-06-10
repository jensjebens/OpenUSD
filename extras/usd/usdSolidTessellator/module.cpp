// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// Module registration for TF_REGISTRY (type system and debug codes).

#include "pxr/pxr.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/debugCodes.h"
#include "pxr/base/tf/debug.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    USDSOLIDTESSELLATOR_BUILDER,
    USDSOLIDTESSELLATOR_MESH
);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(USDSOLIDTESSELLATOR_BUILDER,
        "UsdSolid BrepBuilder: topology reconstruction diagnostics");
    TF_DEBUG_ENVIRONMENT_SYMBOL(USDSOLIDTESSELLATOR_MESH,
        "UsdSolid Tessellator: meshing diagnostics");
}

PXR_NAMESPACE_CLOSE_SCOPE

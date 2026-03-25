//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "usdToNewtonMapper.h"

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdPhysics/rigidBodyAPI.h"

PXR_NAMESPACE_OPEN_SCOPE

void
UsdToNewtonMapper::MapStage(const UsdStageRefPtr &stage)
{
    _bodyCount = 0;

    if (!stage) {
        TF_WARN("UsdToNewtonMapper::MapStage called with null stage.");
        return;
    }

    for (const UsdPrim &prim : stage->Traverse()) {
        if (prim.HasAPI<UsdPhysicsRigidBodyAPI>()) {
            ++_bodyCount;
            TF_STATUS("Found physics rigid body: %s",
                       prim.GetPath().GetText());
        }
    }

    TF_STATUS("UsdToNewtonMapper: mapped %zu rigid bodies.", _bodyCount);
}

PXR_NAMESPACE_CLOSE_SCOPE

//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file dsoOwnership/execDsoOwnership.cpp
/// \brief OpenExec computation for Dynamic Spatial Ownership.
///
/// Registers `computeEffectiveWorldTransform` for DsoDynamicOwnershipAPI.
/// When ownershipMode == "carrier", resolves the prim's effective world
/// transform as: localOffset * carrierWorldTransform, where the active
/// carrier is selected by a time-sampled activeCarrierIndex into a
/// pre-authored multi-target carriers relationship.
///
/// When ownershipMode == "physics", registers a TransformProvider that
/// defers to HdExecComputedTransformSceneIndex's provider registry
/// (e.g., Newton physics).
///
/// When ownershipMode == "authored" or activeCarrierIndex is out of range,
/// falls back to standard hierarchical transform resolution.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"

#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/execGeom/tokens.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/exec/vdf/readIterator.h"

PXR_NAMESPACE_USING_DIRECTIVE

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // Our computation name
    (computeEffectiveWorldTransform)

    // Input name for carrier world transforms (renamed to avoid collision
    // with the NamespaceAncestor input which also uses
    // computeLocalToWorldTransform)
    (carrierWorldTransforms)

    // DynamicOwnershipAPI attribute tokens (namespaced)
    ((ownershipMode,       "dynamicOwnership:ownershipMode"))
    ((localOffset,         "dynamicOwnership:localOffset"))
    ((activeCarrierIndex,  "dynamicOwnership:activeCarrierIndex"))
    ((carriers,            "dynamicOwnership:carriers"))

    // Standard xform ops token (matches execGeom convention)
    ((xformOpsTransform, "xformOps:transform"))

    // Ownership mode values
    (carrier)
    (physics)
    (authored)
);

// ---------------------------------------------------------------------------
// Computation callback
// ---------------------------------------------------------------------------

static GfMatrix4d
_ComputeEffectiveWorldTransform(const VdfContext &ctx)
{
    // Read ownership mode
    const TfToken *const modePtr =
        ctx.GetInputValuePtr<TfToken>(_tokens->ownershipMode);

    const bool isCarrierMode =
        modePtr && (*modePtr == _tokens->carrier);

    if (isCarrierMode) {
        // Read active carrier index
        const int *const indexPtr =
            ctx.GetInputValuePtr<int>(_tokens->activeCarrierIndex);
        const int activeIndex = indexPtr ? *indexPtr : 0;

        // Iterate through carrier world transforms to the active one.
        // The VdfReadIterator yields transforms in relationship target order.
        if (activeIndex >= 0) {
            VdfReadIterator<GfMatrix4d> it(
                ctx, _tokens->carrierWorldTransforms);

            int i = 0;
            for (; !it.IsAtEnd() && i < activeIndex; ++it, ++i) {}

            if (!it.IsAtEnd()) {
                // Found the active carrier's world transform
                const GfMatrix4d &carrierWorld = *it;

                const GfMatrix4d *const localOffset =
                    ctx.GetInputValuePtr<GfMatrix4d>(_tokens->localOffset);

                GfMatrix4d offset =
                    localOffset ? *localOffset : GfMatrix4d(1.0);

                // effective = localOffset * carrierWorld
                return offset * carrierWorld;
            }
        }
        // activeIndex < 0 or out of range: fall through to hierarchical
    }

    // Physics mode: the HdExec filter's TransformProvider registry handles
    // this — physics engines register their own providers. The exec
    // computation returns identity here; the scene index filter checks
    // providers before the exec fallback.

    // Fallback: standard hierarchical transform (replicates execGeom logic).
    // This handles ownershipMode == "authored" and any case where carrier
    // resolution doesn't apply.
    const GfMatrix4d *const localToParent =
        ctx.GetInputValuePtr<GfMatrix4d>(_tokens->xformOpsTransform);

    const GfMatrix4d *const parentToWorld =
        ctx.GetInputValuePtr<GfMatrix4d>(
            ExecGeomXformableTokens->computeLocalToWorldTransform);

    if (parentToWorld) {
        return localToParent
            ? (*localToParent) * (*parentToWorld)
            : (*parentToWorld);
    } else {
        return localToParent
            ? (*localToParent)
            : GfMatrix4d(1.0);
    }
}

// ---------------------------------------------------------------------------
// Computation registration
// ---------------------------------------------------------------------------

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(DsoDynamicOwnershipAPI)
{
    self.PrimComputation(_tokens->computeEffectiveWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeEffectiveWorldTransform)
        .Inputs(
            // DynamicOwnershipAPI attributes
            AttributeValue<TfToken>(
                _tokens->ownershipMode),
            AttributeValue<GfMatrix4d>(
                _tokens->localOffset),
            AttributeValue<int>(
                _tokens->activeCarrierIndex),

            // All carriers' world transforms via multi-target relationship.
            // Follows dynamicOwnership:carriers -> [carrier0, carrier1, ...],
            // requests computeLocalToWorldTransform on each.
            Relationship(_tokens->carriers)
                .TargetedObjects<GfMatrix4d>(
                    ExecGeomXformableTokens->computeLocalToWorldTransform)
                .InputName(_tokens->carrierWorldTransforms),

            // Hierarchical transform inputs (fallback path)
            AttributeValue<GfMatrix4d>(
                _tokens->xformOpsTransform),
            NamespaceAncestor<GfMatrix4d>(
                ExecGeomXformableTokens->computeLocalToWorldTransform)
        );
}

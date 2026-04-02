//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdExec/physicsXformProvider.h"
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/xformSchema.h"

PXR_NAMESPACE_OPEN_SCOPE

HdContainerDataSourceHandle
HdExecPhysicsXformProvider::GetFlattenedDataSource(
    const Context &ctx) const
{
    TF_STATUS("HdExecPhysicsXformProvider::GetFlattenedDataSource "
              "called for %s", ctx.GetPrimPath().GetText());
    static const HdContainerDataSourceHandle identityXform =
        HdXformSchema::Builder()
            .SetMatrix(
                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                    GfMatrix4d().SetIdentity()))
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build();

    // Check if this prim has a cached physics transform.
    // If so, use M_sim as the world-space transform (resetXformStack=true).
    // Children will compose their local xform with this parent's M_sim
    // through the normal flattening path.
    std::optional<GfMatrix4d> cachedTransform =
        HdExecComputedTransformSceneIndex::GetCachedTransform(
            ctx.GetPrimPath());

    if (cachedTransform) {
        return HdXformSchema::Builder()
            .SetMatrix(
                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                    *cachedTransform))
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build();
    }

    // No cached transform — fall through to standard flattening logic.
    // This is equivalent to HdFlattenedXformDataSourceProvider.

    const HdXformSchema inputXform(ctx.GetInputDataSource());

    // If the local xform is fully composed, early out.
    if (HdBoolDataSourceHandle const resetXformStack =
                    inputXform.GetResetXformStack()) {
        if (resetXformStack->GetTypedValue(0.0f)) {
            if (inputXform.GetMatrix()) {
                return inputXform.GetContainer();
            } else {
                return identityXform;
            }
        }
    }

    HdMatrixDataSourceHandle const inputMatrixDataSource =
        inputXform.GetMatrix();

    const HdXformSchema parentXform(ctx.GetFlattenedDataSourceFromParentPrim());
    HdMatrixDataSourceHandle const parentMatrixDataSource =
        parentXform.GetMatrix();

    if (!inputMatrixDataSource && !parentMatrixDataSource) {
        return identityXform;
    } else if (!inputMatrixDataSource) {
        return parentXform.GetContainer();
    } else if (!parentMatrixDataSource) {
        return HdXformSchema::Builder()
            .SetMatrix(inputMatrixDataSource)
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build();
    } else {
        // Concatenate: local × parent.
        GfMatrix4d local = inputMatrixDataSource->GetTypedValue(0);
        GfMatrix4d parent = parentMatrixDataSource->GetTypedValue(0);
        GfMatrix4d result = local * parent;

        return HdXformSchema::Builder()
            .SetMatrix(
                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(result))
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build();
    }
}

void
HdExecPhysicsXformProvider::ComputeDirtyLocatorsForDescendants(
    HdDataSourceLocatorSet *locators) const
{
    *locators = HdDataSourceLocatorSet::UniversalSet();
}

PXR_NAMESPACE_CLOSE_SCOPE

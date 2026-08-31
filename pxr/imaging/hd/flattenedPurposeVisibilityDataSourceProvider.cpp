//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/flattenedPurposeVisibilityDataSourceProvider.h"

#include "pxr/imaging/hd/purposeVisibilitySchema.h"

PXR_NAMESPACE_OPEN_SCOPE

static
HdTokenDataSourceHandle
_Pick(const HdTokenDataSourceHandle &input,
      const HdTokenDataSourceHandle &parent)
{
    return input ? input : parent;
}

HdContainerDataSourceHandle
HdFlattenedPurposeVisibilityDataSourceProvider::GetFlattenedDataSource(
    const Context &ctx) const
{
    HdPurposeVisibilitySchema input(ctx.GetInputDataSource());
    HdPurposeVisibilitySchema parent(
        ctx.GetFlattenedDataSourceFromParentPrim());

    if (!parent.GetContainer()) {
        return input.GetContainer();
    }
    if (!input.GetContainer()) {
        return parent.GetContainer();
    }

    return HdPurposeVisibilitySchema::Builder()
        .SetGuideVisibility(
            _Pick(input.GetGuideVisibility(), parent.GetGuideVisibility()))
        .SetProxyVisibility(
            _Pick(input.GetProxyVisibility(), parent.GetProxyVisibility()))
        .SetRenderVisibility(
            _Pick(input.GetRenderVisibility(), parent.GetRenderVisibility()))
        .Build();
}

void
HdFlattenedPurposeVisibilityDataSourceProvider::
ComputeDirtyLocatorsForDescendants(
    HdDataSourceLocatorSet * const locators) const
{
    *locators = HdDataSourceLocatorSet::UniversalSet();
}

PXR_NAMESPACE_CLOSE_SCOPE

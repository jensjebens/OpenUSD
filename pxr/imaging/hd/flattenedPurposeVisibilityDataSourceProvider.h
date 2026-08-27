//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_FLATTENED_PURPOSE_VISIBILITY_DATA_SOURCE_PROVIDER_H
#define PXR_IMAGING_HD_FLATTENED_PURPOSE_VISIBILITY_DATA_SOURCE_PROVIDER_H

#include "pxr/pxr.h"

#include "pxr/imaging/hd/api.h"
#include "pxr/imaging/hd/flattenedDataSourceProvider.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdFlattenedPurposeVisibilityDataSourceProvider
///
/// Inherits the UsdGeomVisibilityAPI purpose-visibility tokens down namespace.
///
/// Each of guideVisibility, proxyVisibility and renderVisibility inherits
/// independently. An opinion authored on a prim wins for that member, whatever
/// its value: authoring "inherited" is how an author stops an ancestor's
/// opinion and defers to the renderer again. Members with no opinion on this
/// prim take the parent's flattened value.
class HdFlattenedPurposeVisibilityDataSourceProvider :
    public HdFlattenedDataSourceProvider
{
public:
    HD_API
    HdContainerDataSourceHandle GetFlattenedDataSource(
        const Context&) const override;

    HD_API
    void ComputeDirtyLocatorsForDescendants(
        HdDataSourceLocatorSet * locators) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

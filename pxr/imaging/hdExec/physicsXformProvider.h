//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#ifndef PXR_IMAGING_HD_EXEC_PHYSICS_XFORM_PROVIDER_H
#define PXR_IMAGING_HD_EXEC_PHYSICS_XFORM_PROVIDER_H

/// \file hdExec/physicsXformProvider.h
///
/// A physics-aware xform flattening provider that integrates cached
/// simulation transforms from the HdExec static transform cache into
/// the HdFlatteningSceneIndex pass.
///
/// When a prim has a cached physics transform (e.g. from Newton GPU),
/// the provider substitutes M_sim as the prim's world-space transform
/// with resetXformStack=true. Children of physics bodies naturally
/// get the correct composed world matrix through the flattening pass.
///
/// Register via UsdImagingSceneIndexPlugin::FlattenedDataSourceProviders().

#include "pxr/pxr.h"
#include "pxr/imaging/hdExec/api.h"
#include "pxr/imaging/hd/flattenedDataSourceProvider.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdExecPhysicsXformProvider : public HdFlattenedDataSourceProvider
{
public:
    HDEXEC_API
    HdContainerDataSourceHandle GetFlattenedDataSource(
        const Context &ctx) const override;

    HDEXEC_API
    void ComputeDirtyLocatorsForDescendants(
        HdDataSourceLocatorSet *locators) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_EXEC_PHYSICS_XFORM_PROVIDER_H

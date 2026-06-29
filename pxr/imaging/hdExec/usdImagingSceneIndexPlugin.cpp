//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file hdExec/usdImagingSceneIndexPlugin.cpp
///
/// Registers HdExecPhysicsXformProvider with the UsdImaging flattening
/// pipeline via UsdImagingSceneIndexPlugin::FlattenedDataSourceProviders().
///
/// This plugin is auto-discovered by UsdImaging through the TfType registry
/// and plugInfo.json. It injects the physics-aware xform provider into
/// HdFlatteningSceneIndex, enabling cached physics transforms on parent
/// Xforms to propagate correctly to child renderables.

#include "pxr/imaging/hdExec/physicsXformProvider.h"

#include "pxr/usdImaging/usdImaging/sceneIndexPlugin.h"

#include "pxr/imaging/hd/flattenedDataSourceProvider.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/base/tf/registryManager.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdExec_UsdImagingSceneIndexPlugin
    : public UsdImagingSceneIndexPlugin
{
public:
    HdSceneIndexBaseRefPtr AppendSceneIndex(
        HdSceneIndexBaseRefPtr const &inputScene) override
    {
        // No additional scene index to append at UsdImaging level.
        // The HdExec filter is already registered as an HdSceneIndexPlugin.
        return inputScene;
    }

    HdContainerDataSourceHandle
    FlattenedDataSourceProviders() override
    {
        using namespace HdMakeDataSourceContainingFlattenedDataSourceProvider;

        TF_STATUS("HdExec_UsdImagingSceneIndexPlugin::"
                  "FlattenedDataSourceProviders called");

        // Register our physics-aware xform provider. This overrides the
        // default HdFlattenedXformDataSourceProvider for the "xform" key.
        return HdRetainedContainerDataSource::New(
            HdXformSchemaTokens->xform,
            Make<HdExecPhysicsXformProvider>());
    }
};

TF_REGISTRY_FUNCTION(UsdImagingSceneIndexPlugin)
{
    UsdImagingSceneIndexPlugin::Define<HdExec_UsdImagingSceneIndexPlugin>();
}

PXR_NAMESPACE_CLOSE_SCOPE

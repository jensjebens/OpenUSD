//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdExec/sceneIndexPlugin.h"

#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdExec_ComputedTransformSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<
        HdExec_ComputedTransformSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Register for all renderers.
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        /* rendererDisplayName = */ "",
        _tokens->sceneIndexPluginName,
        /* inputArgs = */ nullptr,
        /* insertionPhase = */ 1,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
}

///////////////////////////////////////////////////////////////////////////////

HdExec_ComputedTransformSceneIndexPlugin::
HdExec_ComputedTransformSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdExec_ComputedTransformSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    // The plugin creates the scene index but needs a stage + exec system.
    // For now, return inputScene unchanged — the filter is instantiated
    // explicitly by applications that set up the exec system.
    //
    // TODO: Extract stage from inputArgs when the UsdImaging pipeline
    // supports passing it through.
    return inputScene;
}

PXR_NAMESPACE_CLOSE_SCOPE

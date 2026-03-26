//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdExec/sceneIndexPlugin.h"
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdExec_ComputedTransformSceneIndexPlugin"))
    ((computeSimulatedTransform, "computeSimulatedTransform"))
    ((computeLocalToWorldTransform, "computeLocalToWorldTransform"))
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
    // Create an auto-bootstrapping filter. The filter lazily discovers
    // a UsdStage (via UsdNotice::StageContentsChanged or the static
    // SetGlobalStage) and creates its own ExecUsdSystem. This makes
    // usdrecord (and any USD app) automatically get exec-computed
    // transforms (e.g., physics simulation) without any application code.
    return HdExecComputedTransformSceneIndex::NewAutoBootstrap(
        inputScene,
        {_tokens->computeSimulatedTransform,
         _tokens->computeLocalToWorldTransform},
        /* resetXformStack = */ true);
}

PXR_NAMESPACE_CLOSE_SCOPE

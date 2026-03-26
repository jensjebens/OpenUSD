//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdExec/sceneIndexPlugin.h"
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/stage.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/base/tf/weakBase.h"

PXR_NAMESPACE_OPEN_SCOPE

// Global listener that captures the first stage opened.
// Registered at library load time (before any scene index exists).
namespace {
class _GlobalStageListener : public TfWeakBase {
public:
    _GlobalStageListener() = default;

    void Register() {
        TfNotice::Register(
            TfCreateWeakPtr(this),
            &_GlobalStageListener::_OnStageContentsChanged);
    }

    void _OnStageContentsChanged(
        const UsdNotice::StageContentsChanged &notice)
    {
        UsdStageWeakPtr sender = notice.GetStage();
        if (sender) {
            HdExecComputedTransformSceneIndex::SetGlobalStage(
                UsdStageRefPtr(sender));
        }
    }
};

static _GlobalStageListener *_sGlobalListener = nullptr;
}

// Register the global stage listener early — this catches stages opened
// before any scene index filter is created.
TF_REGISTRY_FUNCTION(HdExecComputedTransformSceneIndex)
{
    static _GlobalStageListener listener;
    listener.Register();
    _sGlobalListener = &listener;
}

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
    // Register for all renderers via empty string.
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        /* rendererDisplayName = */ "",
        _tokens->sceneIndexPluginName,
        /* inputArgs = */ nullptr,
        /* insertionPhase = */ 1,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);

    // Also register explicitly for Storm ("GL") to ensure loading.
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        /* rendererDisplayName = */ "GL",
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

//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdLod/sceneIndexPlugin.h"
#include "pxr/imaging/hdLod/hdLodSceneIndex.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdLod_SceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // insertionPhase = 0 (before HdGp at phase 2, before hdSt plugins)
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        HdSceneIndexPluginRegistryTokens->allRenderers,
        TfToken("HdLod_SceneIndexPlugin"),
        /* inputArgs = */ nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdLod_SceneIndexPlugin::HdLod_SceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdLod_SceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdLodSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

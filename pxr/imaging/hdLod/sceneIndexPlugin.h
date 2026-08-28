//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_LOD_SCENE_INDEX_PLUGIN_H
#define PXR_IMAGING_HD_LOD_SCENE_INDEX_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hdLod/api.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdLod_SceneIndexPlugin
///
/// Plugin that registers HdLodSceneIndex into the Hydra scene index chain
/// for all renderers.
///
class HdLod_SceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HDLOD_API
    HdLod_SceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_LOD_SCENE_INDEX_PLUGIN_H

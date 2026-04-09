//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_EXEC_SCENE_INDEX_PLUGIN_H
#define PXR_IMAGING_HD_EXEC_SCENE_INDEX_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hdExec/api.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdExec_ComputedTransformSceneIndexPlugin
///
/// Scene index plugin that provides infrastructure for registering the
/// OpenExec computed transform scene index filter with the Hydra scene
/// index plugin system.
///
/// The plugin registration exists so the infrastructure is in place, but
/// actual instantiation requires an ExecUsdSystem which needs a stage.
/// In practice, applications (like USDView) will create the filter
/// explicitly with the proper exec system and stage.
///
class HdExec_ComputedTransformSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdExec_ComputedTransformSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_EXEC_SCENE_INDEX_PLUGIN_H

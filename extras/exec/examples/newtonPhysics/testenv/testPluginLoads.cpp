//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testPluginLoads.cpp
/// \brief Minimal test that verifies the Newton physics plugin loads
/// and registers with the Exec system without crashing.

#include "pxr/pxr.h"

#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"

PXR_NAMESPACE_USING_DIRECTIVE

int main()
{
    // Load the Newton physics plugin.
    const PlugPluginPtrVector testPlugins = PlugRegistry::GetInstance()
        .RegisterPlugins(TfAbsPath("resources"));
    TF_AXIOM(testPlugins.size() == 1);
    TF_AXIOM(testPlugins[0]->GetName() == "newtonPhysicsPlugin");

    // Open the fallingBox test asset.
    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    // Create an ExecUsdSystem — verifies the plugin integrates
    // with the Exec runtime without crashing.
    ExecUsdSystem execSystem(stage);

    return 0;
}

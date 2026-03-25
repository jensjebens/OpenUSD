//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "newtonSimulationDriver.h"
#include "newtonWorldManager.h"

#include "pxr/pxr.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/editContext.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformOp.h"
#include "pxr/usd/usdPhysics/scene.h"

PXR_NAMESPACE_OPEN_SCOPE

void
NewtonSimulationDriver::Initialize(const UsdStageRefPtr &stage)
{
    if (!stage) {
        TF_WARN("NewtonSimulationDriver::Initialize called with "
                 "null stage.");
        return;
    }

    // Reset any prior state.
    if (_initialized) {
        Reset();
    }

    _stage = stage;

    // Find the PhysicsScene prim by traversal.
    UsdPhysicsScene physicsScene;
    for (const UsdPrim &prim : stage->Traverse()) {
        physicsScene = UsdPhysicsScene(prim);
        if (physicsScene) {
            break;
        }
    }

    NewtonWorldManager &mgr = NewtonWorldManager::GetInstance();

    if (physicsScene) {
        mgr.Initialize(physicsScene);
    } else {
        TF_WARN("No UsdPhysicsScene found on stage. "
                 "Using default gravity (0, -9.81, 0).");
        mgr.Initialize(GfVec3d(0.0, -9.81, 0.0));
    }

    // Map all physics bodies on the stage.
    _mapper.MapStage(stage);

    // Create an anonymous session sublayer for physics results.
    _sessionLayer = SdfLayer::CreateAnonymous("newtonPhysics.usda");

    // Insert the session layer into the stage's session layer stack.
    // The session layer sits above the root layer in composition,
    // so authored values there override the root layer's values.
    SdfLayerRefPtr sessionRoot = stage->GetSessionLayer();
    sessionRoot->InsertSubLayerPath(_sessionLayer->GetIdentifier());

    _currentSimTime = 0.0;
    _initialized = true;

    TF_STATUS("NewtonSimulationDriver initialized with %zu bodies "
              "(%zu dynamic, %zu static).",
              _mapper.GetBodyCount(),
              _mapper.GetDynamicBodyCount(),
              _mapper.GetStaticBodyCount());
}

void
NewtonSimulationDriver::StepAndWriteBack(double dt)
{
    if (!_initialized) {
        TF_WARN("NewtonSimulationDriver::StepAndWriteBack called "
                 "before initialization.");
        return;
    }

    if (dt <= 0.0) {
        return;
    }

    // Step the physics world.
    NewtonWorldManager::GetInstance().Step(dt);
    _currentSimTime += dt;

    // Write simulated transforms back to the session layer.
    _WriteTransformsToSessionLayer();
}

void
NewtonSimulationDriver::AdvanceToTimeCode(UsdTimeCode time,
                                           double timeCodesPerSecond)
{
    if (!_initialized) {
        TF_WARN("NewtonSimulationDriver::AdvanceToTimeCode called "
                 "before initialization.");
        return;
    }

    if (time.IsDefault() || timeCodesPerSecond <= 0.0) {
        return;
    }

    double targetSeconds = time.GetValue() / timeCodesPerSecond;
    double dt = targetSeconds - _currentSimTime;

    if (dt > 0.0) {
        StepAndWriteBack(dt);
    }
}

void
NewtonSimulationDriver::Reset()
{
    _mapper.Clear();
    NewtonWorldManager::GetInstance().Reset();

    if (_stage && _sessionLayer) {
        // Remove our session sublayer from the stage.
        SdfLayerRefPtr sessionRoot = _stage->GetSessionLayer();
        std::string identifier = _sessionLayer->GetIdentifier();

        SdfSubLayerProxy subLayers = sessionRoot->GetSubLayerPaths();
        for (size_t i = 0; i < subLayers.size(); ++i) {
            if (subLayers[i] == identifier) {
                sessionRoot->RemoveSubLayerPath(i);
                break;
            }
        }
    }

    _sessionLayer = nullptr;
    _stage = nullptr;
    _currentSimTime = 0.0;
    _initialized = false;

    TF_STATUS("NewtonSimulationDriver reset.");
}

void
NewtonSimulationDriver::_WriteTransformsToSessionLayer()
{
    if (!_sessionLayer || !_stage) {
        return;
    }

    // Get all dynamic body paths and write their transforms.
    std::vector<SdfPath> dynamicPaths = _mapper.GetDynamicBodyPaths();

    for (const SdfPath &path : dynamicPaths) {
        GfMatrix4d xform = _mapper.GetSimulatedTransform(path);

        // Extract translation from the 4x4 matrix.
        // Row 3 (index 3) contains the translation in USD's row-major
        // convention: [tx, ty, tz, 1].
        GfVec3d translate(xform[3][0], xform[3][1], xform[3][2]);

        // Author xformOp:translate on the session layer.
        // We use the SdfLayer API directly to write to our specific
        // session sublayer, rather than going through the UsdStage
        // edit target (which would write to the root layer).
        SdfPath attrPath = path.AppendProperty(
            TfToken("xformOp:translate"));

        // Ensure the prim spec exists in the session layer.
        SdfPrimSpecHandle primSpec = SdfCreatePrimInLayer(
            _sessionLayer, path);

        if (primSpec) {
            // Create or find the attribute spec.
            SdfAttributeSpecHandle attrSpec = _sessionLayer->GetAttributeAtPath(
                attrPath);

            if (!attrSpec) {
                attrSpec = SdfAttributeSpec::New(
                    primSpec,
                    "xformOp:translate",
                    SdfValueTypeNames->Double3);
            }

            if (attrSpec) {
                _sessionLayer->SetField(
                    attrPath,
                    SdfFieldKeys->Default,
                    VtValue(translate));
            }
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

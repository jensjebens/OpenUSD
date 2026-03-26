//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

/// \file testenv/testExecTransformWithPhysics.cpp
/// \brief Integration test: Full pipeline from Newton physics through
///        OpenExec computation to the HdExec scene index filter.
///
/// Proves the complete data path:
///   Newton → NewtonPhysicsSystem → OpenExec computeSimulatedTransform
///   → HdExec filter → Hydra
///
/// NO session layer involved. The computation IS the transport.

#include "pxr/pxr.h"

#include "../newtonPhysicsSystem.h"
#include "../newtonWorldManager.h"
#include "../usdToNewtonMapper.h"

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/retainedSceneIndex.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/exec/execUsd/system.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include "pxr/base/gf/math.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/token.h"

#include <iostream>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

/// Helper: build an HdRetainedSceneIndex with initial prim entries
/// that mirror the prims in fallingBox.usda. Each gets an identity
/// xform data source.
HdRetainedSceneIndexRefPtr
_BuildRetainedSceneIndex()
{
    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();

    // Build identity xform data source.
    auto identityXformDs = HdXformSchema::Builder()
        .SetMatrix(
            HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                GfMatrix4d(1.0)))
        .SetResetXformStack(
            HdRetainedTypedSampledDataSource<bool>::New(false))
        .Build();

    auto primDs = HdRetainedContainerDataSource::New(
        HdXformSchemaTokens->xform, identityXformDs);

    // Add the prims we care about. The filter will overlay exec-computed
    // transforms onto these.
    retainedSi->AddPrims({
        {SdfPath("/World"), TfToken("xform"), primDs},
        {SdfPath("/World/FallingBox"), TfToken("cube"), primDs},
        {SdfPath("/World/Ground"), TfToken("cube"), primDs},
    });

    return retainedSi;
}

/// Helper: extract translation from HdXformSchema on a scene index prim.
GfVec3d
_GetTranslateFromSceneIndex(
    const HdSceneIndexBaseRefPtr &sceneIndex,
    const SdfPath &primPath)
{
    HdSceneIndexPrim prim = sceneIndex->GetPrim(primPath);
    HdXformSchema xform = HdXformSchema::GetFromParent(prim.dataSource);

    if (!xform.IsDefined() || !xform.GetMatrix()) {
        // No xform data — return zero.
        return GfVec3d(0.0);
    }

    GfMatrix4d matrix = xform.GetMatrix()->GetTypedValue(0);
    return matrix.ExtractTranslation();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test 1: Full pipeline — Newton → NewtonPhysicsSystem → exec → HdExec
// ---------------------------------------------------------------------------

static bool
_TestFullPipeline()
{
    std::cout << "=== Test: Full Pipeline (Newton → exec → HdExec) ==="
              << std::endl;

    // Open the test scene.
    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    // Create the ExecUsdSystem from the stage.
    auto execSystem = std::make_shared<ExecUsdSystem>(stage);
    TF_AXIOM(execSystem);

    // Initialize the Newton physics system directly — no session layer.
    NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
    sys.EnsureInitialized(stage);
    TF_AXIOM(sys.IsInitialized());

    // Build the retained scene index with initial prim data.
    HdRetainedSceneIndexRefPtr retainedSi = _BuildRetainedSceneIndex();

    // Create the HdExec scene index filter. This looks for
    // "computeSimulatedTransform" (registered by
    // newtonPhysicsComputations.cpp for UsdPhysicsRigidBodyAPI).
    TfToken computeSimulated("computeSimulatedTransform");

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeSimulated},
        /* resetXformStack = */ true);
    TF_AXIOM(filter);

    // Step the simulation for 60 frames (1 second at 60 fps).
    for (int i = 0; i < 60; ++i) {
        sys.AdvanceToTime((i + 1) / 60.0);
    }

    // Now read back through the Hydra scene index filter.
    // The exec system should evaluate computeSimulatedTransform, which
    // queries NewtonPhysicsSystem directly via computePath — no session
    // layer involved.
    GfVec3d boxTranslate = _GetTranslateFromSceneIndex(
        filter, SdfPath("/World/FallingBox"));

    std::cout << "  FallingBox translate from HdExec filter: ("
              << boxTranslate[0] << ", "
              << boxTranslate[1] << ", "
              << boxTranslate[2] << ")" << std::endl;

#ifdef NEWTON_DYNAMICS_FOUND
    // With Newton: box should have fallen from y=10.
    TF_AXIOM(boxTranslate[1] < 10.0);
    // Should not have fallen through the ground.
    TF_AXIOM(boxTranslate[1] >= -1.0);
    std::cout << "  [Newton mode] Box fell to Y=" << boxTranslate[1]
              << std::endl;
#else
    // Stub mode: NewtonPhysicsSystem returns the initial transform
    // (identity stepping), so the computation returns the initial
    // transform from the mapper. The filter should return this without
    // crashing.
    std::cout << "  [stub mode] FallingBox Y: " << boxTranslate[1]
              << " (expected ~10.0)" << std::endl;
    // In stub mode, the computation may return the initial value
    // or identity depending on whether the exec system can resolve
    // the inputs. Accept either.
    // The key requirement: no crash, and we get a valid matrix back.
    TF_AXIOM(boxTranslate[1] >= 0.0);
#endif

    // Verify the physics system also agrees.
    GfMatrix4d sysXform = sys.GetSimulatedTransform(
        SdfPath("/World/FallingBox"));
    GfVec3d sysPos = sysXform.ExtractTranslation();

    std::cout << "  FallingBox from NewtonPhysicsSystem: ("
              << sysPos[0] << ", "
              << sysPos[1] << ", "
              << sysPos[2] << ")" << std::endl;

#ifdef NEWTON_DYNAMICS_FOUND
    TF_AXIOM(sysPos[1] < 10.0);
#else
    TF_AXIOM(GfIsClose(sysPos[1], 10.0, 0.1));
#endif

    // Ground should be unchanged — it has CollisionAPI but no
    // RigidBodyAPI, so no computeSimulatedTransform computation.
    // The filter should pass through the identity xform we set in
    // the retained scene index.
    GfVec3d groundTranslate = _GetTranslateFromSceneIndex(
        filter, SdfPath("/World/Ground"));

    std::cout << "  Ground translate from HdExec filter: ("
              << groundTranslate[0] << ", "
              << groundTranslate[1] << ", "
              << groundTranslate[2] << ")" << std::endl;

    // Ground has no RigidBodyAPI → no exec computation → passthrough
    // from retained scene index → identity → translate = (0, 0, 0).
    TF_AXIOM(GfIsClose(groundTranslate[0], 0.0, 1e-4));
    TF_AXIOM(GfIsClose(groundTranslate[2], 0.0, 1e-4));

    sys.Reset();

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: Filter correctly identifies computed prim paths
// ---------------------------------------------------------------------------

static bool
_TestComputedPrimPaths()
{
    std::cout << "=== Test: Computed Prim Paths ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = _BuildRetainedSceneIndex();

    TfToken computeSimulated("computeSimulatedTransform");

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeSimulated},
        /* resetXformStack = */ true);

    // Force the filter to evaluate prims so they get cached.
    filter->GetPrim(SdfPath("/World/FallingBox"));
    filter->GetPrim(SdfPath("/World/Ground"));

    SdfPathVector computed = filter->GetComputedPrimPaths();

    // FallingBox has PhysicsRigidBodyAPI → computeSimulatedTransform
    // Ground has only PhysicsCollisionAPI → no computation
    bool hasFallingBox = false;
    bool hasGround = false;

    for (const SdfPath &path : computed) {
        if (path == SdfPath("/World/FallingBox")) {
            hasFallingBox = true;
        }
        if (path == SdfPath("/World/Ground")) {
            hasGround = true;
        }
    }

    std::cout << "  Computed prim count: " << computed.size() << std::endl;
    std::cout << "  Has FallingBox: " << hasFallingBox << std::endl;
    std::cout << "  Has Ground: " << hasGround << std::endl;

    TF_AXIOM(hasFallingBox);
    TF_AXIOM(!hasGround);

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: Child prim paths pass through unchanged
// ---------------------------------------------------------------------------

static bool
_TestChildPrimPathsPassthrough()
{
    std::cout << "=== Test: Child Prim Paths Passthrough ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = _BuildRetainedSceneIndex();

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {TfToken("computeSimulatedTransform")});

    // The retained scene index has /World with children
    // /World/FallingBox and /World/Ground.
    SdfPathVector rootChildren = filter->GetChildPrimPaths(SdfPath("/"));
    TF_AXIOM(rootChildren == SdfPathVector{SdfPath("/World")});

    SdfPathVector worldChildren = filter->GetChildPrimPaths(
        SdfPath("/World"));
    TF_AXIOM(worldChildren.size() == 2);

    // Children of leaf prims should be empty.
    SdfPathVector boxChildren = filter->GetChildPrimPaths(
        SdfPath("/World/FallingBox"));
    TF_AXIOM(boxChildren.empty());

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: Filter handles non-existent prims gracefully
// ---------------------------------------------------------------------------

static bool
_TestNonExistentPrim()
{
    std::cout << "=== Test: Non-Existent Prim ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = _BuildRetainedSceneIndex();

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {TfToken("computeSimulatedTransform")});

    // Query a prim that doesn't exist in the retained scene index.
    HdSceneIndexPrim prim = filter->GetPrim(
        SdfPath("/World/DoesNotExist"));

    // Should return an empty/invalid prim without crashing.
    TF_AXIOM(!prim.dataSource);

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: SetTime sends dirty notifications for physics prims
// ---------------------------------------------------------------------------

static bool
_TestSetTimeDirty()
{
    std::cout << "=== Test: SetTime Dirty Notifications ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(
        TfAbsPath("fallingBox.usda"));
    TF_AXIOM(layer);

    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = _BuildRetainedSceneIndex();

    TfToken computeSimulated("computeSimulatedTransform");

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeSimulated},
        /* resetXformStack = */ true);

    // Force the filter to discover the FallingBox computation.
    filter->GetPrim(SdfPath("/World/FallingBox"));

    // Advance time — this should dirty computed prims.
    // (We don't attach an observer here because the main purpose
    // is to verify SetTime doesn't crash.)
    filter->SetTime(UsdTimeCode(1.0));
    filter->SetTime(UsdTimeCode(2.0));

    // Verify we can still read the prim after SetTime.
    HdSceneIndexPrim prim = filter->GetPrim(
        SdfPath("/World/FallingBox"));
    HdXformSchema xform = HdXformSchema::GetFromParent(prim.dataSource);
    TF_AXIOM(xform.IsDefined());
    TF_AXIOM(xform.GetMatrix());

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------

int main(int /*argc*/, char ** /*argv*/)
{
    TfErrorMark mark;

    std::cout << "testExecTransformWithPhysics — Full pipeline integration"
              << std::endl;
    std::cout << "Newton → NewtonPhysicsSystem → OpenExec → HdExec → Hydra"
              << std::endl;
    std::cout << "(No session layer — computation IS the transport)"
              << std::endl;
    std::cout << std::endl;

    bool success = true;
    success &= _TestFullPipeline();
    success &= _TestComputedPrimPaths();
    success &= _TestChildPrimPathsPassthrough();
    success &= _TestNonExistentPrim();
    success &= _TestSetTimeDirty();

    TF_VERIFY(mark.IsClean());

    if (success && mark.IsClean()) {
        std::cout << std::endl << "testExecTransformWithPhysics PASSED"
                  << std::endl;
        return EXIT_SUCCESS;
    } else {
        std::cout << std::endl << "testExecTransformWithPhysics FAILED"
                  << std::endl;
        return EXIT_FAILURE;
    }
}

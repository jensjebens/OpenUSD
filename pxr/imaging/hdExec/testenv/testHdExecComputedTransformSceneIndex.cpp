//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/retainedSceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/token.h"

#include <iostream>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// ---------------------------------------------------------------------------
// RecordingSceneIndexObserver
// ---------------------------------------------------------------------------
// Lofted from testHdsiPrefixPathPruningSceneIndex.cpp

class RecordingSceneIndexObserver : public HdSceneIndexObserver
{
public:

    enum EventType {
        EventType_PrimAdded = 0,
        EventType_PrimRemoved,
        EventType_PrimDirtied,
    };

    struct Event
    {
        EventType eventType;
        SdfPath primPath;
        TfToken primType;
        HdDataSourceLocator locator;

        inline bool operator==(Event const &rhs) const noexcept
        {
            return (
                eventType == rhs.eventType
                && primPath == rhs.primPath
                && primType == rhs.primType
                && locator == rhs.locator);
        }

        template <class HashState>
        friend void TfHashAppend(HashState &h, Event const &myObj) {
            h.Append(myObj.eventType);
            h.Append(myObj.primPath);
            h.Append(myObj.primType);
            h.Append(myObj.locator);
        }

        inline size_t Hash() const;
        struct HashFunctor {
            size_t operator()(Event const& event) const {
                return event.Hash();
            }
        };
    };

    using EventVector = std::vector<Event>;
    using EventSet = std::unordered_set<Event, Event::HashFunctor>;

    void PrimsAdded(
        const HdSceneIndexBase &sender,
        const AddedPrimEntries &entries) override
    {
        for (const AddedPrimEntry &entry : entries) {
            _events.emplace_back(Event{
                EventType_PrimAdded, entry.primPath, entry.primType});
        }
    }

    void PrimsRemoved(
        const HdSceneIndexBase &sender,
        const RemovedPrimEntries &entries) override
    {
        for (const RemovedPrimEntry &entry : entries) {
            _events.emplace_back(
                Event{EventType_PrimRemoved, entry.primPath});
        }
    }

    void PrimsDirtied(
        const HdSceneIndexBase &sender,
        const DirtiedPrimEntries &entries) override
    {
        for (const DirtiedPrimEntry &entry : entries) {
            for (const HdDataSourceLocator &locator : entry.dirtyLocators) {
                _events.emplace_back(Event{
                    EventType_PrimDirtied,
                    entry.primPath, TfToken(), locator});
            }
        }
    }

    void PrimsRenamed(
        const HdSceneIndexBase &sender,
        const RenamedPrimEntries &entries) override
    {
        ConvertPrimsRenamedToRemovedAndAdded(sender, entries, this);
    }

    EventVector GetEvents()
    {
        return _events;
    }

    void Clear()
    {
        _events.clear();
    }

private:
    EventVector _events;
};

inline size_t
RecordingSceneIndexObserver::Event::Hash() const
{
    return TfHash()(*this);
}

// ---------------------------------------------------------------------------
// Test 1: Passthrough — prims without exec computations pass through.
// ---------------------------------------------------------------------------

static bool
_TestPassthrough()
{
    std::cout << "=== TestPassthrough ===" << std::endl;

    // Create a retained scene with a prim that has an xform data source.
    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();

    GfMatrix4d translateMatrix(1.0);
    translateMatrix.SetTranslate(GfVec3d(1, 2, 3));

    auto xformDs = HdXformSchema::Builder()
        .SetMatrix(
            HdRetainedTypedSampledDataSource<GfMatrix4d>::New(translateMatrix))
        .SetResetXformStack(
            HdRetainedTypedSampledDataSource<bool>::New(false))
        .Build();

    auto primDs = HdRetainedContainerDataSource::New(
        HdXformSchemaTokens->xform, xformDs);
    retainedSi->AddPrims(
        {{SdfPath("/Foo"), TfToken("mesh"), primDs}});

    // Create a stage with a Scope (no xform computations available).
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
def Scope "Foo" {}
)usda");
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    // Create our filter looking for a computation that won't exist on Scope.
    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {TfToken("computeSimulatedTransform")});

    // GetPrim should return the original xform unchanged.
    HdSceneIndexPrim prim = filter->GetPrim(SdfPath("/Foo"));
    HdXformSchema xform = HdXformSchema::GetFromParent(prim.dataSource);
    TF_AXIOM(xform.IsDefined());

    GfMatrix4d matrix = xform.GetMatrix()->GetTypedValue(0);
    TF_AXIOM(GfIsClose(
        matrix.ExtractTranslation(), GfVec3d(1, 2, 3), 1e-6));

    // GetChildPrimPaths passthrough.
    TF_AXIOM(filter->GetChildPrimPaths(SdfPath("/"))
        == SdfPathVector{SdfPath("/Foo")});

    // GetComputedPrimPaths should be empty (Scope has no computations).
    TF_AXIOM(filter->GetComputedPrimPaths().empty());

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: Exec transform override — prims with exec computations get
//         their xform overlaid.
// ---------------------------------------------------------------------------

static bool
_TestExecTransformOverride()
{
    std::cout << "=== TestExecTransformOverride ===" << std::endl;

    // Create a stage with an Xform prim that has a known transform.
    // The execGeom library registers computeLocalToWorldTransform for
    // UsdGeomXformable.
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
def Xform "Parent" {
    matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(10,20,30,1))
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
)usda");
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    // Create retained scene with an identity xform for /Parent —
    // the exec computation should override this.
    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();
    auto identityDs = HdXformSchema::Builder()
        .SetMatrix(
            HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                GfMatrix4d(1.0)))
        .Build();
    auto primDs = HdRetainedContainerDataSource::New(
        HdXformSchemaTokens->xform, identityDs);
    retainedSi->AddPrims(
        {{SdfPath("/Parent"), TfToken("xform"), primDs}});

    // Use the computation token directly (avoiding link to execGeom for the
    // token — we use a literal TfToken like the exec test does).
    TfToken computeLocalToWorld("computeLocalToWorldTransform");

    // Create filter looking for computeLocalToWorldTransform (from execGeom).
    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeLocalToWorld},
        /* resetXformStack = */ true);

    // GetPrim should return the exec-computed transform, NOT identity.
    HdSceneIndexPrim prim = filter->GetPrim(SdfPath("/Parent"));
    HdXformSchema xform = HdXformSchema::GetFromParent(prim.dataSource);
    TF_AXIOM(xform.IsDefined());

    GfMatrix4d matrix = xform.GetMatrix()->GetTypedValue(0);
    GfVec3d translate = matrix.ExtractTranslation();

    // The exec computation should produce translate(10, 20, 30).
    std::cout << "  Computed translate: ("
              << translate[0] << ", "
              << translate[1] << ", "
              << translate[2] << ")" << std::endl;
    TF_AXIOM(GfIsClose(translate, GfVec3d(10, 20, 30), 1e-6));

    // resetXformStack should be true.
    TF_AXIOM(xform.GetResetXformStack());
    TF_AXIOM(xform.GetResetXformStack()->GetTypedValue(0) == true);

    // GetComputedPrimPaths should contain /Parent.
    SdfPathVector computed = filter->GetComputedPrimPaths();
    TF_AXIOM(computed.size() == 1);
    TF_AXIOM(computed[0] == SdfPath("/Parent"));

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: Dirty notification — SetTime sends dirty notifications for
//         computed prims.
// ---------------------------------------------------------------------------

static bool
_TestDirtyNotification()
{
    std::cout << "=== TestDirtyNotification ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
def Xform "Parent" {
    matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(10,20,30,1))
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
)usda");
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();
    auto identityDs = HdXformSchema::Builder()
        .SetMatrix(
            HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                GfMatrix4d(1.0)))
        .Build();
    auto primDs = HdRetainedContainerDataSource::New(
        HdXformSchemaTokens->xform, identityDs);
    retainedSi->AddPrims(
        {{SdfPath("/Parent"), TfToken("xform"), primDs}});

    TfToken computeLocalToWorld("computeLocalToWorldTransform");

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeLocalToWorld},
        /* resetXformStack = */ true);

    // Force the filter to discover the computation.
    filter->GetPrim(SdfPath("/Parent"));

    // Add an observer.
    RecordingSceneIndexObserver observer;
    filter->AddObserver(HdSceneIndexObserverPtr(&observer));

    // Advance time.
    filter->SetTime(UsdTimeCode(1.0));

    // Verify we got an xform dirty notification for /Parent.
    auto events = observer.GetEvents();
    bool foundXformDirty = false;
    for (const auto &e : events) {
        if (e.eventType ==
                RecordingSceneIndexObserver::EventType_PrimDirtied
            && e.primPath == SdfPath("/Parent")
            && e.locator == HdXformSchema::GetDefaultLocator()) {
            foundXformDirty = true;
        }
    }
    TF_AXIOM(foundXformDirty);

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: Multiple prims — xformable prims get overrides, scopes don't.
// ---------------------------------------------------------------------------

static bool
_TestMultiplePrims()
{
    std::cout << "=== TestMultiplePrims ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
def Xform "A" {
    matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(1,0,0,1))
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
def Scope "B" {
}
def Xform "C" {
    matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,5,1))
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
)usda");
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();
    auto identityDs = HdXformSchema::Builder()
        .SetMatrix(
            HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                GfMatrix4d(1.0)))
        .Build();
    auto primDs = HdRetainedContainerDataSource::New(
        HdXformSchemaTokens->xform, identityDs);

    retainedSi->AddPrims({
        {SdfPath("/A"), TfToken("xform"), primDs},
        {SdfPath("/B"), TfToken("scope"), primDs},
        {SdfPath("/C"), TfToken("xform"), primDs},
    });

    TfToken computeLocalToWorld("computeLocalToWorldTransform");

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeLocalToWorld},
        /* resetXformStack = */ true);

    // /A should have exec override.
    {
        HdSceneIndexPrim primA = filter->GetPrim(SdfPath("/A"));
        HdXformSchema xformA =
            HdXformSchema::GetFromParent(primA.dataSource);
        TF_AXIOM(xformA.IsDefined());
        GfMatrix4d matA = xformA.GetMatrix()->GetTypedValue(0);
        TF_AXIOM(GfIsClose(
            matA.ExtractTranslation(), GfVec3d(1, 0, 0), 1e-6));
    }

    // /B (Scope) should pass through with identity.
    {
        HdSceneIndexPrim primB = filter->GetPrim(SdfPath("/B"));
        HdXformSchema xformB =
            HdXformSchema::GetFromParent(primB.dataSource);
        TF_AXIOM(xformB.IsDefined());
        GfMatrix4d matB = xformB.GetMatrix()->GetTypedValue(0);
        TF_AXIOM(GfIsClose(
            matB.ExtractTranslation(), GfVec3d(0, 0, 0), 1e-6));
    }

    // /C should have exec override.
    {
        HdSceneIndexPrim primC = filter->GetPrim(SdfPath("/C"));
        HdXformSchema xformC =
            HdXformSchema::GetFromParent(primC.dataSource);
        TF_AXIOM(xformC.IsDefined());
        GfMatrix4d matC = xformC.GetMatrix()->GetTypedValue(0);
        TF_AXIOM(GfIsClose(
            matC.ExtractTranslation(), GfVec3d(0, 0, 5), 1e-6));
    }

    // Verify computed prim paths contain A and C but not B.
    SdfPathVector computed = filter->GetComputedPrimPaths();
    TF_AXIOM(computed.size() == 2);

    bool hasA = false, hasC = false;
    for (const auto &p : computed) {
        if (p == SdfPath("/A")) hasA = true;
        if (p == SdfPath("/C")) hasC = true;
    }
    TF_AXIOM(hasA && hasC);

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: GetChildPrimPaths always delegates to input (no topology changes).
// ---------------------------------------------------------------------------

static bool
_TestChildPrimPaths()
{
    std::cout << "=== TestChildPrimPaths ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
def Xform "Parent" {
    matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(1,0,0,1))
    uniform token[] xformOpOrder = ["xformOp:transform"]

    def Xform "Child" {
        matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1))
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
}
)usda");
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();
    auto primDs = HdRetainedContainerDataSource::New();
    retainedSi->AddPrims({
        {SdfPath("/Parent"), TfToken("xform"), primDs},
        {SdfPath("/Parent/Child"), TfToken("xform"), primDs},
    });

    TfToken computeLocalToWorld("computeLocalToWorldTransform");

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {computeLocalToWorld});

    // GetChildPrimPaths should always delegate to input unchanged.
    SdfPathVector rootChildren = filter->GetChildPrimPaths(SdfPath("/"));
    TF_AXIOM(rootChildren == SdfPathVector{SdfPath("/Parent")});

    SdfPathVector parentChildren =
        filter->GetChildPrimPaths(SdfPath("/Parent"));
    TF_AXIOM(parentChildren == SdfPathVector{SdfPath("/Parent/Child")});

    SdfPathVector childChildren =
        filter->GetChildPrimPaths(SdfPath("/Parent/Child"));
    TF_AXIOM(childChildren.empty());

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Test 6: Forwarding of observer notifications (PrimsAdded/Removed/Dirtied).
// ---------------------------------------------------------------------------

static bool
_TestNotificationForwarding()
{
    std::cout << "=== TestNotificationForwarding ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
def Scope "A" {}
)usda");
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    auto execSystem = std::make_shared<ExecUsdSystem>(stage);

    HdRetainedSceneIndexRefPtr retainedSi = HdRetainedSceneIndex::New();
    auto primDs = HdRetainedContainerDataSource::New();
    retainedSi->AddPrims(
        {{SdfPath("/A"), TfToken("scope"), primDs}});

    auto filter = HdExecComputedTransformSceneIndex::New(
        retainedSi,
        stage,
        execSystem,
        {TfToken("computeSimulatedTransform")});

    RecordingSceneIndexObserver observer;
    filter->AddObserver(HdSceneIndexObserverPtr(&observer));

    // Add a new prim.
    retainedSi->AddPrims(
        {{SdfPath("/B"), TfToken("mesh"), primDs}});

    {
        auto events = observer.GetEvents();
        TF_AXIOM(!events.empty());
        bool foundAdd = false;
        for (const auto &e : events) {
            if (e.eventType ==
                    RecordingSceneIndexObserver::EventType_PrimAdded
                && e.primPath == SdfPath("/B")) {
                foundAdd = true;
            }
        }
        TF_AXIOM(foundAdd);
    }

    observer.Clear();

    // Remove a prim.
    retainedSi->RemovePrims(
        {{SdfPath("/B")}});

    {
        auto events = observer.GetEvents();
        TF_AXIOM(!events.empty());
        bool foundRemove = false;
        for (const auto &e : events) {
            if (e.eventType ==
                    RecordingSceneIndexObserver::EventType_PrimRemoved
                && e.primPath == SdfPath("/B")) {
                foundRemove = true;
            }
        }
        TF_AXIOM(foundRemove);
    }

    std::cout << "  PASSED" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    TfErrorMark mark;

    bool success = true;
    success &= _TestPassthrough();
    success &= _TestExecTransformOverride();
    success &= _TestDirtyNotification();
    success &= _TestMultiplePrims();
    success &= _TestChildPrimPaths();
    success &= _TestNotificationForwarding();

    TF_VERIFY(mark.IsClean());

    if (success && mark.IsClean()) {
        std::cout << "OK" << std::endl;
        return EXIT_SUCCESS;
    } else {
        std::cout << "FAILED" << std::endl;
        return EXIT_FAILURE;
    }
}

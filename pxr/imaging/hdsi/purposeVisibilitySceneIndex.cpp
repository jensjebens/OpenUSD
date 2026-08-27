//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdsi/purposeVisibilitySceneIndex.h"

#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/purposeSchema.h"
#include "pxr/imaging/hd/purposeVisibilitySchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/visibilitySchema.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (inherited)
    (invisible)
    (visible)
);

namespace {

// The purposeVisibility member gating this prim, or null when the prim's
// purpose has no corresponding member.
HdTokenDataSourceHandle
_MemberForPurpose(const HdPurposeVisibilitySchema &pv, const TfToken &purpose)
{
    if (purpose == HdRenderTagTokens->guide) {
        return pv.GetGuideVisibility();
    }
    if (purpose == HdRenderTagTokens->proxy) {
        return pv.GetProxyVisibility();
    }
    if (purpose == HdRenderTagTokens->render) {
        return pv.GetRenderVisibility();
    }
    return nullptr;
}

// True when purpose visibility hides this prim.
bool
_IsHidden(const HdContainerDataSourceHandle &primSource)
{
    const HdPurposeSchema purposeSchema =
        HdPurposeSchema::GetFromParent(primSource);
    HdTokenDataSourceHandle const purposeDs = purposeSchema.GetPurpose();
    if (!purposeDs) {
        return false;
    }
    const TfToken purpose = purposeDs->GetTypedValue(0.0f);

    const HdPurposeVisibilitySchema pv =
        HdPurposeVisibilitySchema::GetFromParent(primSource);

    HdTokenDataSourceHandle const memberDs = _MemberForPurpose(pv, purpose);

    if (purpose == HdRenderTagTokens->guide) {
        // A guide is drawn only on an explicit "visible".
        return !memberDs ||
               memberDs->GetTypedValue(0.0f) != _tokens->visible;
    }

    if (!memberDs) {
        return false;
    }
    return memberDs->GetTypedValue(0.0f) == _tokens->invisible;
}

const HdContainerDataSourceHandle &
_InvisibleOverlay()
{
    static const HdContainerDataSourceHandle overlay =
        HdRetainedContainerDataSource::New(
            HdVisibilitySchema::GetSchemaToken(),
            HdVisibilitySchema::Builder()
                .SetVisibility(
                    HdRetainedTypedSampledDataSource<bool>::New(false))
                .Build());
    return overlay;
}

}

HdsiPurposeVisibilitySceneIndexRefPtr
HdsiPurposeVisibilitySceneIndex::New(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(
        new HdsiPurposeVisibilitySceneIndex(inputSceneIndex));
}

HdsiPurposeVisibilitySceneIndex::HdsiPurposeVisibilitySceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
  : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
}

HdSceneIndexPrim
HdsiPurposeVisibilitySceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (prim.dataSource && _IsHidden(prim.dataSource)) {
        prim.dataSource = HdOverlayContainerDataSource::New(
            _InvisibleOverlay(), prim.dataSource);
    }
    return prim;
}

SdfPathVector
HdsiPurposeVisibilitySceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdsiPurposeVisibilitySceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    _SendPrimsAdded(entries);
}

void
HdsiPurposeVisibilitySceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    _SendPrimsRemoved(entries);
}

void
HdsiPurposeVisibilitySceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    // A change to purpose or to purposeVisibility changes the visibility this
    // scene index computes, so forward it as a visibility dirty as well.
    HdSceneIndexObserver::DirtiedPrimEntries extra;
    for (const HdSceneIndexObserver::DirtiedPrimEntry &entry : entries) {
        if (entry.dirtyLocators.Intersects(
                HdPurposeVisibilitySchema::GetDefaultLocator()) ||
            entry.dirtyLocators.Intersects(
                HdPurposeSchema::GetDefaultLocator())) {
            extra.emplace_back(
                entry.primPath, HdVisibilitySchema::GetDefaultLocator());
        }
    }
    if (extra.empty()) {
        _SendPrimsDirtied(entries);
        return;
    }
    HdSceneIndexObserver::DirtiedPrimEntries all(entries);
    all.insert(all.end(), extra.begin(), extra.end());
    _SendPrimsDirtied(all);
}

PXR_NAMESPACE_CLOSE_SCOPE

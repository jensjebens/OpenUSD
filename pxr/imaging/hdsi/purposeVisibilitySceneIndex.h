//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HDSI_PURPOSE_VISIBILITY_SCENE_INDEX_H
#define PXR_IMAGING_HDSI_PURPOSE_VISIBILITY_SCENE_INDEX_H

#include "pxr/pxr.h"

#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hdsi/api.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(HdsiPurposeVisibilitySceneIndex);

/// \class HdsiPurposeVisibilitySceneIndex
///
/// Applies the UsdGeomVisibilityAPI purpose-visibility tokens to visibility.
///
/// A prim's purpose selects one member of the purposeVisibility schema. For
/// the proxy and render purposes the prim is hidden when that member resolves
/// to "invisible"; "inherited" and "visible" leave visibility alone, so the
/// render task's purpose set still decides whether the prim is drawn at all.
/// The guide purpose inverts: a guide is hidden unless guideVisibility
/// resolves to "visible".
///
/// Purpose visibility only subtracts. It can hide geometry the renderer would
/// otherwise draw, and never reveals geometry whose purpose the render task
/// excludes.
///
/// Place this after flattening, so both purpose and purposeVisibility have
/// already been inherited down namespace.
class HdsiPurposeVisibilitySceneIndex final
    : public HdSingleInputFilteringSceneIndexBase
{
public:
    HDSI_API
    static HdsiPurposeVisibilitySceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HDSI_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    HDSI_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HdsiPurposeVisibilitySceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex);

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override;
    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override;
    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

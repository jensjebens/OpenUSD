//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/flattenedVisibilityDataSourceProvider.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/imaging/hd/visibilitySchema.h"

PXR_NAMESPACE_OPEN_SCOPE

// Helper: resolve a single boolean field with inheritance.
// Returns local if present, else parent's value.
static HdBoolDataSourceHandle
_ResolveField(
    const HdBoolDataSourceHandle &local,
    const HdBoolDataSourceHandle &parent)
{
    if (local) {
        return local;
    }
    return parent;
}

HdContainerDataSourceHandle
HdFlattenedVisibilityDataSourceProvider::GetFlattenedDataSource(
    const Context &ctx) const
{
    HdVisibilitySchema inputVisibility(ctx.GetInputDataSource());
    HdVisibilitySchema parentVisibility(
        ctx.GetFlattenedDataSourceFromParentPrim());

    // Resolve base visibility (existing logic).
    HdBoolDataSourceHandle baseVis = inputVisibility.GetVisibility();
    if (!baseVis) {
        baseVis = parentVisibility.GetVisibility();
    }

    // Resolve purpose visibility fields — inherit from parent if no
    // local opinion.  Base visibility overrides: if base is explicitly
    // false (invisible), purpose visibility is forced invisible.
    auto resolveForPurpose =
        [&](const HdBoolDataSourceHandle &localPurpose,
            const HdBoolDataSourceHandle &parentPurpose)
            -> HdBoolDataSourceHandle
    {
        // If base visibility is explicitly invisible, purpose is invisible
        // regardless of any purpose visibility opinion.
        if (baseVis) {
            if (!baseVis->GetTypedValue(0.0f)) {
                return baseVis; // false — invisible overrides everything
            }
        }

        return _ResolveField(localPurpose, parentPurpose);
    };

    HdBoolDataSourceHandle guideVis = resolveForPurpose(
        inputVisibility.GetGuideVisibility(),
        parentVisibility.GetGuideVisibility());

    HdBoolDataSourceHandle proxyVis = resolveForPurpose(
        inputVisibility.GetProxyVisibility(),
        parentVisibility.GetProxyVisibility());

    HdBoolDataSourceHandle renderVis = resolveForPurpose(
        inputVisibility.GetRenderVisibility(),
        parentVisibility.GetRenderVisibility());

    // If nothing is authored (no base vis, no purpose vis), return
    // default visible.
    if (!baseVis && !guideVis && !proxyVis && !renderVis) {
        static const HdContainerDataSourceHandle identityVisibility =
            HdVisibilitySchema::Builder()
                .SetVisibility(
                    HdRetainedTypedSampledDataSource<bool>::New(true))
                .Build();
        return identityVisibility;
    }

    // Build result with all resolved fields.
    HdVisibilitySchema::Builder builder;
    if (baseVis) {
        builder.SetVisibility(baseVis);
    } else {
        builder.SetVisibility(
            HdRetainedTypedSampledDataSource<bool>::New(true));
    }
    if (guideVis) {
        builder.SetGuideVisibility(guideVis);
    }
    if (proxyVis) {
        builder.SetProxyVisibility(proxyVis);
    }
    if (renderVis) {
        builder.SetRenderVisibility(renderVis);
    }

    return builder.Build();
}

void
HdFlattenedVisibilityDataSourceProvider::ComputeDirtyLocatorsForDescendants(
    HdDataSourceLocatorSet * const locators) const
{
    *locators = HdDataSourceLocatorSet::UniversalSet();
}

PXR_NAMESPACE_CLOSE_SCOPE

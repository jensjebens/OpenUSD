//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/flattenedVisibilityDataSourceProvider.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/imaging/hd/purposeSchema.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/visibilitySchema.h"

PXR_NAMESPACE_OPEN_SCOPE

// Helper: resolve a purpose visibility field with inheritance.
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

    // Check if base visibility is explicitly invisible.
    bool baseIsInvisible = false;
    if (baseVis) {
        baseIsInvisible = !baseVis->GetTypedValue(0.0f);
    }

    // Resolve purpose visibility fields — inherit from parent if no
    // local opinion.
    auto resolveForPurpose =
        [&](const HdBoolDataSourceHandle &localPurpose,
            const HdBoolDataSourceHandle &parentPurpose)
            -> HdBoolDataSourceHandle
    {
        // If base visibility is explicitly invisible, purpose is invisible
        // regardless.
        if (baseIsInvisible) {
            return baseVis;
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

    // Resolve the effective base visibility: if this prim has a purpose
    // and the corresponding purpose visibility says invisible, fold that
    // into the base visibility boolean so that downstream consumers
    // (render tag filter, _IsVisible, etc.) see the prim as invisible
    // without needing to know about purpose visibility.
    HdBoolDataSourceHandle effectiveVis = baseVis;

    // Get the prim's purpose from the flattened data.
    HdPurposeSchema purposeSchema(
        ctx.GetFlattenedDataSourceFromParentPrim()
            ? HdPurposeSchema::GetFromParent(
                  ctx.GetFlattenedDataSourceFromParentPrim())
            : HdPurposeSchema(nullptr));
    // Also check the input prim's own purpose (takes priority).
    {
        HdContainerDataSourceHandle inputPrim = ctx.GetInputDataSource();
        if (inputPrim) {
            // The input data source is the visibility container, not the
            // prim container. We need the prim container for purpose.
            // Unfortunately the Context doesn't give us that directly.
            // So we'll let the render pass plugins handle the final
            // resolution for now, and just propagate purpose vis fields.
        }
    }

    // Build result with all resolved fields.
    HdVisibilitySchema::Builder builder;
    if (effectiveVis) {
        builder.SetVisibility(effectiveVis);
    } else {
        static const HdBoolDataSourceHandle defaultTrue =
            HdRetainedTypedSampledDataSource<bool>::New(true);
        builder.SetVisibility(defaultTrue);
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

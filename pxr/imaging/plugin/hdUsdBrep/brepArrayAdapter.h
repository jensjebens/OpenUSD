// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// UsdImaging adapter for BrepArray prims. Maps them to Hydra prim type
// "generativeProcedural" so hdGp resolves them via our tessellation plugin.

#ifndef PXR_IMAGING_PLUGIN_HD_USD_BREP_BREP_ARRAY_ADAPTER_H
#define PXR_IMAGING_PLUGIN_HD_USD_BREP_BREP_ARRAY_ADAPTER_H

#include "api.h"

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/instanceablePrimAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdUsdBrepArrayAdapter
///
/// UsdImaging adapter that maps BrepArray prims to Hydra's
/// "generativeProcedural" prim type, enabling the hdGp resolving
/// scene index to invoke HdUsdBrepTessellationProceduralPlugin.
///
/// The adapter also injects a synthetic "hdGp:proceduralType" primvar
/// so BrepArray USD files need no extra authoring for Hydra visualization.
///
class HdUsdBrepArrayAdapter
    : public UsdImagingInstanceablePrimAdapter
{
public:
    using BaseAdapter = UsdImagingInstanceablePrimAdapter;

    // ---------------------------------------------------------------------- //
    /// \name Scene Index Support (modern path — Storm uses this)
    // ---------------------------------------------------------------------- //

    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    TfToken GetImagingSubprimType(
        UsdPrim const& prim, TfToken const& subprim) override;

    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals) override;

    HdDataSourceLocatorSet InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType) override;

    // ---------------------------------------------------------------------- //
    /// \name Legacy Render Index Support
    // ---------------------------------------------------------------------- //

    SdfPath Populate(
        UsdPrim const& prim,
        UsdImagingIndexProxy* index,
        UsdImagingInstancerContext const*
            instancerContext = nullptr) override;

    bool IsSupported(UsdImagingIndexProxy const* index) const override;

    void TrackVariability(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        HdDirtyBits* timeVaryingBits,
        UsdImagingInstancerContext const*
            instancerContext = nullptr) const override;

    void UpdateForTime(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdTimeCode time,
        HdDirtyBits requestedBits,
        UsdImagingInstancerContext const*
            instancerContext = nullptr) const override;

    HdDirtyBits ProcessPropertyChange(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        TfToken const& propertyName) override;

    void MarkDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        HdDirtyBits dirty,
        UsdImagingIndexProxy* index) override;

    void MarkTransformDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index) override;

    void MarkVisibilityDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index) override;

protected:
    void _RemovePrim(
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_PLUGIN_HD_USD_BREP_BREP_ARRAY_ADAPTER_H

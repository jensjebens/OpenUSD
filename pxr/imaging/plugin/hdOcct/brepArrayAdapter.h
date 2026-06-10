// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// UsdImaging adapter for BrepArray prims. Maps them to Hydra prim type
// "generativeProcedural" so hdGp resolves them via our tessellation plugin.

#ifndef USD_SOLID_TESSELLATOR_BREP_ARRAY_ADAPTER_H
#define USD_SOLID_TESSELLATOR_BREP_ARRAY_ADAPTER_H

#include "api.h"

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/instanceablePrimAdapter.h"
#include "pxr/imaging/hd/dataSource.h"

#include <mutex>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdSolidBrepArrayAdapter
///
/// UsdImaging adapter that maps BrepArray prims to Hydra's
/// "generativeProcedural" prim type, enabling the hdGp resolving
/// scene index to invoke UsdSolidTessellationProceduralPlugin.
///
/// The adapter also injects a synthetic "hdGp:proceduralType" primvar
/// so BrepArray USD files need no extra authoring for Hydra visualization.
///
class USDSOLIDTESSELLATOR_API UsdSolidBrepArrayAdapter
    : public UsdImagingInstanceablePrimAdapter
{
public:
    using BaseAdapter = UsdImagingInstanceablePrimAdapter;

    // ---------------------------------------------------------------------- //
    /// \name Scene Index Support (modern path — Storm uses this)
    // ---------------------------------------------------------------------- //

    USDSOLIDTESSELLATOR_API
    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    USDSOLIDTESSELLATOR_API
    TfToken GetImagingSubprimType(
        UsdPrim const& prim, TfToken const& subprim) override;

    USDSOLIDTESSELLATOR_API
    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals) override;

    USDSOLIDTESSELLATOR_API
    HdDataSourceLocatorSet InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType) override;

    // ---------------------------------------------------------------------- //
    /// \name Legacy Render Index Support
    // ---------------------------------------------------------------------- //

    USDSOLIDTESSELLATOR_API
    SdfPath Populate(
        UsdPrim const& prim,
        UsdImagingIndexProxy* index,
        UsdImagingInstancerContext const*
            instancerContext = nullptr) override;

    USDSOLIDTESSELLATOR_API
    bool IsSupported(UsdImagingIndexProxy const* index) const override;

    USDSOLIDTESSELLATOR_API
    void TrackVariability(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        HdDirtyBits* timeVaryingBits,
        UsdImagingInstancerContext const*
            instancerContext = nullptr) const override;

    USDSOLIDTESSELLATOR_API
    void UpdateForTime(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdTimeCode time,
        HdDirtyBits requestedBits,
        UsdImagingInstancerContext const*
            instancerContext = nullptr) const override;

    USDSOLIDTESSELLATOR_API
    HdDirtyBits ProcessPropertyChange(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        TfToken const& propertyName) override;

    USDSOLIDTESSELLATOR_API
    void MarkDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        HdDirtyBits dirty,
        UsdImagingIndexProxy* index) override;

    USDSOLIDTESSELLATOR_API
    void MarkTransformDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index) override;

    USDSOLIDTESSELLATOR_API
    void MarkVisibilityDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index) override;

protected:
    USDSOLIDTESSELLATOR_API
    void _RemovePrim(
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index) override;

private:
    // Tessellation cache: keyed by prim path, stores computed data source.
    // Invalidated when brep: attributes change (via ProcessPropertyChange).
    mutable std::mutex _cacheMutex;
    mutable std::unordered_map<SdfPath, HdContainerDataSourceHandle,
                               SdfPath::Hash> _tessellationCache;

    HdContainerDataSourceHandle _BuildTessellationDataSource(
        UsdPrim const& prim) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // USD_SOLID_TESSELLATOR_BREP_ARRAY_ADAPTER_H

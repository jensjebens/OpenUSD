// UnitsCorrection.h — Unit correction for Fabric population transforms
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jens Jebens / NVIDIA
//
// Provides _ApplyUnitsCorrection() for ConcurrentXformCache to correct
// transform translations based on source layer metersPerUnit vs stage
// metersPerUnit during USD → Fabric population.
//
// Design:
//   - Per-prim correction factor cached in thread-safe concurrent map
//   - PrimIndex walk only happens once per prim path (then cached)
//   - No-op fast path when source and stage units match
//   - Only translation component (row 3, cols 0-2) is scaled
//   - Rotation/scale components of the matrix are untouched

#pragma once

#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/pcp/primIndex.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <tbb/concurrent_hash_map.h>

namespace usdrt
{
namespace population
{
namespace units
{

/// Thread-safe cache of per-prim unit correction scale factors.
///
/// Scale factor = source_layer_metersPerUnit / stage_metersPerUnit.
/// A value of 1.0 means no correction needed.
/// A value of 0.0 means "not computed yet" (sentinel, never valid for mPU).
class UnitsCorrectionCache
{
public:
    UnitsCorrectionCache() = default;

    /// Clear cached correction factors (call on stage change).
    void Clear()
    {
        m_cache.clear();
        m_stageMpu = 0.0;
    }

    /// Set the stage metersPerUnit. Call once when the cache is created
    /// or when the stage changes.
    void SetStageMpu(double mpu)
    {
        m_stageMpu = mpu;
    }

    /// Get the correction scale factor for a prim.
    /// Returns 1.0 if no correction needed (same units or no source info).
    /// Thread-safe: uses TBB concurrent_hash_map.
    double GetCorrectionFactor(const PXR_NS::UsdPrim& prim)
    {
        if (m_stageMpu <= 0.0)
        {
            return 1.0;
        }

        // Check cache first (read-only, no lock contention on hit)
        {
            Cache::const_accessor accessor;
            if (m_cache.find(accessor, prim.GetPath()))
            {
                return accessor->second;
            }
        }

        // Cache miss: compute and store
        double factor = _ComputeCorrectionFactor(prim);
        {
            Cache::accessor accessor;
            m_cache.insert(accessor, prim.GetPath());
            accessor->second = factor;
        }
        return factor;
    }

    /// Apply unit correction to a local transform matrix.
    /// Scales translation component (row 3, cols 0-2) by the correction factor.
    /// Returns the matrix unchanged if no correction needed.
    static PXR_NS::GfMatrix4d ApplyCorrection(const PXR_NS::GfMatrix4d& localXform, double factor)
    {
        if (std::abs(factor - 1.0) < 1e-10)
        {
            return localXform;
        }

        PXR_NS::GfMatrix4d corrected(localXform);
        // Scale translation only (row 3, columns 0-2)
        corrected[3][0] *= factor;
        corrected[3][1] *= factor;
        corrected[3][2] *= factor;
        return corrected;
    }

private:
    double _ComputeCorrectionFactor(const PXR_NS::UsdPrim& prim) const
    {
        // Walk PrimIndex to find source layer metersPerUnit
        const PXR_NS::PcpPrimIndex& pi = prim.GetPrimIndex();

        for (const PXR_NS::PcpNodeRef& node : pi.GetRootNode().GetChildren())
        {
            PXR_NS::PcpArcType arcType = node.GetArcType();
            if (arcType == PXR_NS::PcpArcTypeReference ||
                arcType == PXR_NS::PcpArcTypePayload)
            {
                const PXR_NS::PcpLayerStackRefPtr& layerStack = node.GetLayerStack();
                if (layerStack && !layerStack->GetLayers().empty())
                {
                    const PXR_NS::SdfLayerHandle& srcLayer = layerStack->GetLayers()[0];
                    // Read metersPerUnit from the source layer's pseudoroot
                    PXR_NS::VtValue mpu;
                    if (srcLayer->HasField(PXR_NS::SdfPath::AbsoluteRootPath(),
                                           PXR_NS::UsdGeomTokens->metersPerUnit,
                                           &mpu))
                    {
                        double srcMpu = mpu.Get<double>();
                        if (std::abs(srcMpu - m_stageMpu) > 1e-10 && srcMpu > 0.0)
                        {
                            return srcMpu / m_stageMpu;
                        }
                    }
                }
            }
        }

        // No reference/payload arc with different units found.
        // Check if parent has a correction (inheritance through hierarchy).
        PXR_NS::UsdPrim parent = prim.GetParent();
        if (parent && parent.IsValid() &&
            parent.GetPath() != PXR_NS::SdfPath::AbsoluteRootPath())
        {
            // Non-const cast: GetCorrectionFactor is logically const
            // but updates the cache.
            return const_cast<UnitsCorrectionCache*>(this)->GetCorrectionFactor(parent);
        }

        return 1.0; // No correction
    }

    struct SdfPathHash
    {
        static size_t hash(const PXR_NS::SdfPath& p)
        {
            return PXR_NS::SdfPath::Hash()(p);
        }
        static bool equal(const PXR_NS::SdfPath& a, const PXR_NS::SdfPath& b)
        {
            return a == b;
        }
    };

    using Cache = tbb::concurrent_hash_map<PXR_NS::SdfPath, double, SdfPathHash>;
    Cache m_cache;
    double m_stageMpu = 0.0;
};

} // namespace units
} // namespace population
} // namespace usdrt

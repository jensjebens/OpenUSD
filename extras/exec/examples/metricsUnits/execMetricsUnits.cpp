//
// execMetricsUnits.cpp — Unit-aware transform computation using MetricsAPI.
//
// Reads metersPerUnit from UsdGeomMetricsAPI (via metrics:metersPerUnit
// attribute) and corrects the transform relative to the nearest ancestor
// that also declares metersPerUnit.
//
// The computation:
// 1. Reads computeLocalToWorldTransform from execGeom (same prim)
// 2. Reads metrics:metersPerUnit from this prim (via GeomMetricsAPI)
// 3. Reads the nearest ancestor's metersPerUnit via NamespaceAncestor
//    (typically the stage root prim with GeomMetricsAPI applied)
// 4. Applies uniform scaling by (primMPU / ancestorMPU) to position
//    and the upper-left 3x3 of the L2W matrix.
//
// No stage metadata access needed — everything is declared in-scene
// via GeomMetricsAPI applied to the root prim and individual assets.
//
// Registration: on GeomMetricsAPI schema — the computation only runs
// for prims that have GeomMetricsAPI applied.
//
#include "pxr/pxr.h"

#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/vdf/context.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // Our computation names
    (computeUnitAwareLocalToWorldTransform)
    (computeMetersPerUnit)

    // The standard L2W from execGeom
    (computeLocalToWorldTransform)

    // MetricsAPI attribute (from GeomMetricsAPI schema)
    ((metersPerUnit, "metrics:metersPerUnit"))

    // Input name for ancestor's metersPerUnit
    (ancestorMetersPerUnit)
);

// USD default: centimeters (metersPerUnit = 0.01)
static constexpr double _USD_DEFAULT_METERS_PER_UNIT = 0.01;


// Helper computation: outputs this prim's metersPerUnit.
// Allows NamespaceAncestor to walk up and find the nearest ancestor's MPU.
static double
_ComputeMetersPerUnit(const VdfContext &ctx)
{
    const double *const mpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->metersPerUnit);

    if (mpuPtr && *mpuPtr > 0.0) {
        return *mpuPtr;
    }

    return _USD_DEFAULT_METERS_PER_UNIT;
}


static GfMatrix4d
_ComputeUnitAwareLocalToWorldTransform(const VdfContext &ctx)
{
    // Get the standard L2W from execGeom
    const GfMatrix4d *const localToWorldPtr =
        ctx.GetInputValuePtr<GfMatrix4d>(
            _tokens->computeLocalToWorldTransform);

    if (!localToWorldPtr) {
        return GfMatrix4d(1.0);
    }

    GfMatrix4d result = *localToWorldPtr;

    // Get prim's metersPerUnit from GeomMetricsAPI
    const double *const primMpuPtr =
        ctx.GetInputValuePtr<double>(
            _tokens->metersPerUnit);

    if (!primMpuPtr || *primMpuPtr <= 0.0) {
        return result;  // No metrics — return raw transform
    }

    const double primMpu = *primMpuPtr;

    // Get the stage/ancestor metersPerUnit via NamespaceAncestor.
    // This walks up the prim hierarchy to find the nearest ancestor
    // with GeomMetricsAPI that provides computeMetersPerUnit.
    // If no ancestor declares it, fall back to USD default (0.01 = cm).
    double stageMpu = _USD_DEFAULT_METERS_PER_UNIT;

    const double *const ancestorMpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->ancestorMetersPerUnit);

    if (ancestorMpuPtr && *ancestorMpuPtr > 0.0) {
        stageMpu = *ancestorMpuPtr;
    }

    if (primMpu == stageMpu) {
        return result;  // No conversion needed
    }

    // Apply UNIFORM scaling — both translation and the upper-left 3x3.
    //
    // A prim authored in meters referenced into a centimeter stage:
    //   primMPU = 1.0, stageMPU = 0.01 → scale = 100
    //   translate (−2, 0.5, 0) × 100 = (−200, 50, 0) cm
    //   3×3 scaled 100× → 1m cone appears as 100cm ✓
    //
    // A prim authored in cm referenced into a meter stage:
    //   primMPU = 0.01, stageMPU = 1.0 → scale = 0.01
    //   translate (100, 0, 50) × 0.01 = (1, 0, 0.5) m
    //   3×3 scaled 0.01× → 100cm cone appears as 1m ✓
    const double scale = primMpu / stageMpu;

    // Scale the upper-left 3x3 (rotation + scale components)
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result[row][col] *= scale;
        }
    }

    // Scale the translation (row 3, columns 0-2)
    GfVec3d translate = result.ExtractTranslation();
    translate *= scale;
    result.SetRow3(3, translate);

    return result;
}


// Register on GeomMetricsAPI — only runs for prims with the schema applied
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdMetricsGeomMetricsAPI)
{
    // Helper computation: outputs this prim's metersPerUnit.
    // Used by NamespaceAncestor to provide ancestor context.
    self.PrimComputation(_tokens->computeMetersPerUnit)
        .Callback<double>(&_ComputeMetersPerUnit)
        .Inputs(
            AttributeValue<double>(_tokens->metersPerUnit)
        );

    // Main computation: corrects the L2W transform based on the ratio
    // of this prim's metersPerUnit to the nearest ancestor's.
    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareLocalToWorldTransform)
        .Inputs(
            // Standard L2W from execGeom (same prim, cross-schema)
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),

            // metersPerUnit from GeomMetricsAPI (prim attribute)
            AttributeValue<double>(
                _tokens->metersPerUnit),

            // Nearest ancestor's metersPerUnit (via NamespaceAncestor)
            NamespaceAncestor<double>(
                _tokens->computeMetersPerUnit)
                .InputName(_tokens->ancestorMetersPerUnit)
        );
}

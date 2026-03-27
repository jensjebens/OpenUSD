//
// execMetricsUnits.cpp — Unit-aware transform computation using MetricsAPI.
//
// Reads metersPerUnit from UsdGeomMetricsAPI (via metrics:metersPerUnit
// attribute) and corrects the transform. Replaces the old
// UnitsResolutionAPI approach with proper prim-level schema attributes.
//
// The computation:
// 1. Reads computeLocalToWorldTransform from execGeom (same prim)
// 2. Reads metrics:metersPerUnit from this prim (authored by GeomMetricsAPI)
// 3. Reads the stage-level metersPerUnit via a stage computation
//    (or falls back to 1.0 if not available)
// 4. Scales the translation component by (prim_mpu / stage_mpu)
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

    // Our computation name
    (computeUnitAwareLocalToWorldTransform)

    // The standard L2W from execGeom
    (computeLocalToWorldTransform)

    // MetricsAPI attributes (from GeomMetricsAPI schema)
    ((metersPerUnit, "metrics:metersPerUnit"))
);


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

    // TODO: Get stage-level metersPerUnit via Stage().Computation<>()
    // For now, assume stage is in meters (1.0). This is correct for
    // the common case and will be replaced when we wire up the stage
    // computation.
    const double stageMpu = 1.0;

    if (primMpu == stageMpu) {
        return result;  // No conversion needed
    }

    // Scale the translation component
    const double scale = primMpu / stageMpu;
    GfVec3d translate = result.ExtractTranslation();
    translate *= scale;
    result.SetRow3(3, translate);

    return result;
}


// Register on GeomMetricsAPI — only runs for prims with the schema applied
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdMetricsGeomMetricsAPI)
{
    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareLocalToWorldTransform)
        .Inputs(
            // Standard L2W from execGeom (same prim, cross-schema)
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),

            // metersPerUnit from GeomMetricsAPI (prim attribute)
            AttributeValue<double>(
                _tokens->metersPerUnit)
        );
}

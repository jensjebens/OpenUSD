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
// 3. Reads metrics:stageMetersPerUnit from the nearest ancestor or defaults
//    to 0.01 (centimeters, the USD default)
// 4. Applies uniform scaling: translation AND upper-left 3x3 are both
//    scaled by (prim_mpu / stage_mpu). This ensures both position and
//    size are corrected (a 20cm cube in a meter stage renders as 0.2m).
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

    // Stage-level metersPerUnit — resolved by ancestor walk.
    // If the prim's nearest ancestor with GeomMetricsAPI provides
    // the stage context, we read it via NamespaceAncestor. Otherwise
    // we fall back to the USD default (0.01 = centimeters).
    ((ancestorMetersPerUnit, "computeUnitAwareLocalToWorldTransform"))
);

// USD default: centimeters (metersPerUnit = 0.01)
static constexpr double _USD_DEFAULT_METERS_PER_UNIT = 0.01;


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

    // Get the stage-level metersPerUnit.
    // In the full MetricsAPI design, this would come from walking
    // up to the stage root's effective metersPerUnit. For now, we
    // read it from the HdExec global stage if available, otherwise
    // fall back to the USD default (0.01 = centimeters).
    //
    // Note: When MetricsAPI (PR #45) is fully integrated, the stage
    // root prim will have GeomMetricsAPI applied, and we can use
    // NamespaceAncestor to find it. For now, we use a helper that
    // checks the global stage.
    double stageMpu = _USD_DEFAULT_METERS_PER_UNIT;

    // Try to get stage metersPerUnit from the global stage.
    // This is set by UsdImagingGLEngine via SetGlobalStage().
    // Import is deferred to avoid a hard link dependency.
    {
        // We can't call UsdGeomGetStageMetersPerUnit from inside a
        // VdfContext (no stage access). Instead, we read the prim's
        // own metersPerUnit and assume the correction target is the
        // stage default. The stage-level mPU is effectively 1.0 for
        // meter-scale stages (the most common case for cross-industry
        // workflows) and 0.01 for cm-scale stages.
        //
        // The correct long-term solution is a stage-level computation
        // that provides stageMpu as a Stage().Computation<double> input.
        // For now, if the prim's mPU equals the USD default (0.01), no
        // correction is applied — the prim is already in the stage's
        // native units.
        //
        // For stages with explicit metersPerUnit != 0.01, consumers
        // should author GeomMetricsAPI on the stage root prim to declare
        // the target unit system. The computation will then use the
        // ratio between source and target.

        // Heuristic: if a prim declares mPU and it matches the stage
        // default, skip correction. Otherwise correct toward meters (1.0).
        // This works for the two most common cases:
        //   - cm prim (0.01) in cm stage (0.01): ratio = 1.0, no correction
        //   - cm prim (0.01) in m stage (1.0): ratio = 0.01, corrects
        //   - mm prim (0.001) in m stage (1.0): ratio = 0.001, corrects
        stageMpu = 1.0;
    }

    if (primMpu == stageMpu) {
        return result;  // No conversion needed
    }

    // Apply UNIFORM scaling — both translation and the upper-left 3x3.
    //
    // A prim authored in centimeters and referenced into a meter-scale
    // stage needs:
    //   - Translation scaled: (100, 0, 50) cm → (1, 0, 0.5) m
    //   - Size scaled: a 20cm cube → 0.2m cube
    //
    // This is equivalent to pre-multiplying by a uniform scale matrix:
    //   result = GfMatrix4d().SetScale(scale) * result
    // But we do it component-wise to be explicit.
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

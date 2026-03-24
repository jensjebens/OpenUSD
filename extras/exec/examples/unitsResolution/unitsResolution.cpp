//
// execUnitsXformable.cpp — Unit-aware local-to-world transform computation.
//
// Registers a computation on a custom UnitsAPI applied schema.
// When UnitsAPI is applied to a prim, it provides the
// computeUnitAwareLocalToWorldTransform computation.
//
// ARCHITECTURAL FINDINGS:
//
// 1. OpenExec computations are pure functions of declared inputs —
//    no access to UsdPrim, PrimIndex, or layer metadata from VdfContext.
//
// 2. Can't register two plugins for the same schema — execGeom already
//    owns UsdGeomXformable computations.
//
// 3. metersPerUnit is layer metadata, not prim metadata — can't be read
//    as an AttributeValue input. This is the gap MetricsAPI (PR #45) fills.
//
// APPROACH:
// Define a custom "UnitsResolutionAPI" applied schema with a unitScale
// attribute. When applied to a prim, the computation reads the unitScale
// and corrects the transform. In production, MetricsAPI would replace
// this with proper prim-level metersPerUnit.
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

    // The standard computation from execGeom (input)
    (computeLocalToWorldTransform)

    // The unit scale attribute on UnitsResolutionAPI
    // Represents: source_metersPerUnit / stage_metersPerUnit
    ((unitScale, "unitsResolution:metersPerUnitScale"))
);


/// Compute the unit-aware local-to-world transform.
///
/// Reads the standard local-to-world from execGeom (via NamespaceAncestor
/// to find the nearest computed transform) and applies the unit correction
/// factor from the unitScale attribute.
static GfMatrix4d
_ComputeUnitAwareLocalToWorldTransform(const VdfContext &ctx)
{
    // Get the standard local-to-world from the existing execGeom computation
    const GfMatrix4d *const localToWorldPtr =
        ctx.GetInputValuePtr<GfMatrix4d>(
            _tokens->computeLocalToWorldTransform);

    if (!localToWorldPtr) {
        return GfMatrix4d(1.0);
    }

    GfMatrix4d result = *localToWorldPtr;

    // Get the unit scale factor
    const double *const unitScalePtr =
        ctx.GetInputValuePtr<double>(
            _tokens->unitScale);

    // Apply unit correction to translation if scale != 1.0
    if (unitScalePtr && *unitScalePtr != 1.0) {
        const double scale = *unitScalePtr;
        GfVec3d translate = result.ExtractTranslation();
        translate *= scale;
        result.SetRow3(3, translate);
    }

    return result;
}


// Register on our own API schema — does NOT conflict with execGeom's
// UsdGeomXformable registration.
//
// In the test scene, prims must have:
//   apiSchemas = ["UnitsResolutionAPI"]
//   double unitsResolution:metersPerUnitScale = <scale_factor>
//
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UnitsResolutionAPI)
{
    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareLocalToWorldTransform)
        .Inputs(
            // The standard local-to-world from execGeom — ON THE SAME PRIM
            // Computation<T> with Local traversal finds computations provided
            // by any schema on the same prim (UsdGeomXformable in this case)
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),

            // The unit scale factor authored on this prim
            AttributeValue<double>(
                _tokens->unitScale)
        );
}

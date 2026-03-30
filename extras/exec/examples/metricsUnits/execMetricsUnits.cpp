//
// execMetricsUnits.cpp — Unit-aware transform computation using MetricsAPI.
//
// Corrects transforms for prims with mismatched metersPerUnit relative
// to the stage context declared on an ancestor prim.
//
// The computation:
// 1. Reads computeLocalToWorldTransform from execGeom (same prim)
// 2. Reads metrics:metersPerUnit from this prim (via GeomMetricsAPI)
// 3. Reads the nearest ancestor's metersPerUnit via NamespaceAncestor
// 4. Applies uniform scaling by (primMPU / ancestorMPU) to the L2W.
//
// No stage metadata access needed — everything is declared in-scene
// via GeomMetricsAPI applied to the root prim and individual assets.
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

    // Computation names
    (computeUnitAwareLocalToWorldTransform)
    (computeMetersPerUnit)

    // The standard L2W from execGeom
    (computeLocalToWorldTransform)

    // MetricsAPI attribute
    ((metersPerUnit, "metrics:metersPerUnit"))

    // Input name for ancestor's metersPerUnit
    (ancestorMetersPerUnit)
);

// USD default: centimeters (metersPerUnit = 0.01)
static constexpr double _USD_DEFAULT_METERS_PER_UNIT = 0.01;


// Helper computation: outputs this prim's metersPerUnit.
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
    const GfMatrix4d *const localToWorldPtr =
        ctx.GetInputValuePtr<GfMatrix4d>(
            _tokens->computeLocalToWorldTransform);

    if (!localToWorldPtr) {
        return GfMatrix4d(1.0);
    }

    GfMatrix4d result = *localToWorldPtr;

    // Get prim's metersPerUnit
    const double *const primMpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->metersPerUnit);

    if (!primMpuPtr || *primMpuPtr <= 0.0) {
        return result;
    }

    const double primMpu = *primMpuPtr;

    // Get ancestor's metersPerUnit via NamespaceAncestor
    double stageMpu = _USD_DEFAULT_METERS_PER_UNIT;
    const double *const ancestorMpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->ancestorMetersPerUnit);
    if (ancestorMpuPtr && *ancestorMpuPtr > 0.0) {
        stageMpu = *ancestorMpuPtr;
    }

    if (primMpu == stageMpu) {
        return result;
    }

    const double scale = primMpu / stageMpu;

    // Scale the upper-left 3x3
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result[row][col] *= scale;
        }
    }

    // Scale the translation
    GfVec3d translate = result.ExtractTranslation();
    translate *= scale;
    result.SetRow3(3, translate);

    return result;
}


EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdMetricsGeomMetricsAPI)
{
    self.PrimComputation(_tokens->computeMetersPerUnit)
        .Callback<double>(&_ComputeMetersPerUnit)
        .Inputs(
            AttributeValue<double>(_tokens->metersPerUnit)
        );

    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareLocalToWorldTransform)
        .Inputs(
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),
            AttributeValue<double>(
                _tokens->metersPerUnit),
            NamespaceAncestor<double>(
                _tokens->computeMetersPerUnit)
                .InputName(_tokens->ancestorMetersPerUnit)
        );
}

//
// execMetricsUnits.cpp — Unit-aware transform computation using MetricsAPI.
//
// Corrects transforms for prims with mismatched metersPerUnit relative
// to the stage context, using inherited resolution via self-referencing
// NamespaceAncestor (same pattern as execGeom's L2W computation).
//
// Inheritance semantics:
//   metersPerUnit = 0 → "inherited" (use ancestor's value)
//   Falls back to USD default: 0.01 (cm)
//
// upAxis correction is deferred pending fix for multi-prim exec graph
// compilation conflict (see metrics-inherited-attrs-plan.md).
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

    (computeUnitAwareLocalToWorldTransform)
    (computeEffectiveMetersPerUnit)

    (computeLocalToWorldTransform)

    ((metersPerUnit, "metrics:metersPerUnit"))

    (ancestorEffectiveMpu)
);

static constexpr double _USD_DEFAULT_MPU = 0.01;


// Effective metersPerUnit: self-referencing NamespaceAncestor for
// inheritance resolution. Returns own value if > 0, else ancestor's,
// else USD default (0.01 = centimeters).
static double
_ComputeEffectiveMetersPerUnit(const VdfContext &ctx)
{
    const double *const myMpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->metersPerUnit);

    if (myMpuPtr && *myMpuPtr > 0.0) {
        return *myMpuPtr;
    }

    const double *const ancestorPtr =
        ctx.GetInputValuePtr<double>(
            _tokens->computeEffectiveMetersPerUnit);

    if (ancestorPtr && *ancestorPtr > 0.0) {
        return *ancestorPtr;
    }

    return _USD_DEFAULT_MPU;
}


static GfMatrix4d
_ComputeUnitAwareLocalToWorldTransform(const VdfContext &ctx)
{
    const GfMatrix4d *const l2wPtr =
        ctx.GetInputValuePtr<GfMatrix4d>(
            _tokens->computeLocalToWorldTransform);

    if (!l2wPtr) {
        return GfMatrix4d(1.0);
    }

    GfMatrix4d result = *l2wPtr;

    double primMpu = 0.0;
    double stageMpu = 0.0;

    const double *const primMpuPtr =
        ctx.GetInputValuePtr<double>(
            _tokens->computeEffectiveMetersPerUnit);
    if (primMpuPtr && *primMpuPtr > 0.0) {
        primMpu = *primMpuPtr;
    }

    const double *const ancestorMpuPtr =
        ctx.GetInputValuePtr<double>(
            _tokens->ancestorEffectiveMpu);
    if (ancestorMpuPtr && *ancestorMpuPtr > 0.0) {
        stageMpu = *ancestorMpuPtr;
    }

    if (primMpu <= 0.0 || stageMpu <= 0.0 || primMpu == stageMpu) {
        return result;
    }

    const double scale = primMpu / stageMpu;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result[row][col] *= scale;
        }
    }

    GfVec3d translate = result.ExtractTranslation();
    translate *= scale;
    result.SetRow3(3, translate);

    return result;
}


EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdMetricsGeomMetricsAPI)
{
    // Effective metersPerUnit with self-referencing NamespaceAncestor
    self.PrimComputation(_tokens->computeEffectiveMetersPerUnit)
        .Callback<double>(&_ComputeEffectiveMetersPerUnit)
        .Inputs(
            AttributeValue<double>(_tokens->metersPerUnit),
            NamespaceAncestor<double>(
                _tokens->computeEffectiveMetersPerUnit)
        );

    // Main transform computation
    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareLocalToWorldTransform)
        .Inputs(
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),
            Computation<double>(
                _tokens->computeEffectiveMetersPerUnit),
            NamespaceAncestor<double>(
                _tokens->computeEffectiveMetersPerUnit)
                .InputName(_tokens->ancestorEffectiveMpu)
        );
}

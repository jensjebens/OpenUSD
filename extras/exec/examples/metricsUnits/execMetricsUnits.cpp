//
// execMetricsUnits.cpp — Unit-aware transform computation using MetricsAPI.
//
// Corrects transforms for prims with mismatched units (metersPerUnit)
// and/or up-axis orientation (upAxis) relative to the stage context.
//
// The computation:
// 1. Reads computeLocalToWorldTransform from execGeom (same prim)
// 2. Reads metrics:metersPerUnit and metrics:upAxis from this prim
// 3. Reads the nearest ancestor's values via NamespaceAncestor
// 4. Applies up-axis rotation (if mismatched) then uniform scaling
//    (if metersPerUnit differs) to the L2W matrix.
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
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // Computation names
    (computeUnitAwareLocalToWorldTransform)
    (computeMetersPerUnit)
    (computeUpAxis)

    // The standard L2W from execGeom
    (computeLocalToWorldTransform)

    // MetricsAPI attributes (from GeomMetricsAPI schema)
    ((metersPerUnit, "metrics:metersPerUnit"))
    ((upAxis, "metrics:upAxis"))

    // Input names for ancestor values
    (ancestorMetersPerUnit)
    (ancestorUpAxis)

    // Up-axis tokens
    (Y)
    (Z)
);

// USD defaults
static constexpr double _USD_DEFAULT_METERS_PER_UNIT = 0.01;  // centimeters
static const TfToken &_USD_DEFAULT_UP_AXIS = _tokens->Y;


// ── Helper computations ─────────────────────────────────────────────────

// Outputs this prim's metersPerUnit for NamespaceAncestor traversal.
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

// Outputs this prim's upAxis for NamespaceAncestor traversal.
static TfToken
_ComputeUpAxis(const VdfContext &ctx)
{
    const TfToken *const upAxisPtr =
        ctx.GetInputValuePtr<TfToken>(_tokens->upAxis);

    if (upAxisPtr && !upAxisPtr->IsEmpty()) {
        return *upAxisPtr;
    }
    return _USD_DEFAULT_UP_AXIS;
}


// ── Up-axis rotation matrices ───────────────────────────────────────────
//
// Z-up → Y-up: rotate -90° around X
//   X →  X,  Y → Z,  Z → -Y  (but we want Z→Y so it's +Z maps to +Y)
//
//   [ 1   0   0   0 ]
//   [ 0   0  -1   0 ]     (cos(-90)=0, sin(-90)=-1)
//   [ 0   1   0   0 ]
//   [ 0   0   0   1 ]
//
// Y-up → Z-up: rotate +90° around X (the inverse)
//
//   [ 1   0   0   0 ]
//   [ 0   0   1   0 ]
//   [ 0  -1   0   0 ]
//   [ 0   0   0   1 ]
//

// Returns the rotation matrix to convert from primUpAxis to stageUpAxis.
// Returns identity if no conversion needed.
static GfMatrix4d
_GetUpAxisCorrectionMatrix(const TfToken &primUpAxis, const TfToken &stageUpAxis)
{
    if (primUpAxis == stageUpAxis) {
        return GfMatrix4d(1.0);
    }

    if (primUpAxis == _tokens->Z && stageUpAxis == _tokens->Y) {
        // Z-up prim in Y-up stage: rotate -90° around X
        // This maps: +Z → +Y (up), +Y → -Z (forward flip)
        GfMatrix4d rot(1.0);
        rot.SetRotate(GfRotation(GfVec3d(1, 0, 0), -90.0));
        return rot;
    }

    if (primUpAxis == _tokens->Y && stageUpAxis == _tokens->Z) {
        // Y-up prim in Z-up stage: rotate +90° around X
        GfMatrix4d rot(1.0);
        rot.SetRotate(GfRotation(GfVec3d(1, 0, 0), 90.0));
        return rot;
    }

    // Unknown combination — no correction
    return GfMatrix4d(1.0);
}


// ── Main computation ────────────────────────────────────────────────────

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

    // ── Up-axis correction ──────────────────────────────────────────

    TfToken primUpAxis = _USD_DEFAULT_UP_AXIS;
    TfToken stageUpAxis = _USD_DEFAULT_UP_AXIS;

    const TfToken *const primUpAxisPtr =
        ctx.GetInputValuePtr<TfToken>(_tokens->upAxis);
    if (primUpAxisPtr && !primUpAxisPtr->IsEmpty()) {
        primUpAxis = *primUpAxisPtr;
    }

    const TfToken *const ancestorUpAxisPtr =
        ctx.GetInputValuePtr<TfToken>(_tokens->ancestorUpAxis);
    if (ancestorUpAxisPtr && !ancestorUpAxisPtr->IsEmpty()) {
        stageUpAxis = *ancestorUpAxisPtr;
    }

    if (primUpAxis != stageUpAxis) {
        // Apply rotation correction.
        // Pre-multiply: correctedL2W = rotationMatrix * rawL2W
        // This rotates the prim's entire coordinate frame to match
        // the stage's up-axis convention.
        GfMatrix4d rotation = _GetUpAxisCorrectionMatrix(primUpAxis, stageUpAxis);
        result = result * rotation;
    }

    // ── Scale correction ────────────────────────────────────────────

    const double *const primMpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->metersPerUnit);

    if (!primMpuPtr || *primMpuPtr <= 0.0) {
        return result;  // No metersPerUnit — return (possibly rotated) transform
    }

    const double primMpu = *primMpuPtr;

    double stageMpu = _USD_DEFAULT_METERS_PER_UNIT;
    const double *const ancestorMpuPtr =
        ctx.GetInputValuePtr<double>(_tokens->ancestorMetersPerUnit);
    if (ancestorMpuPtr && *ancestorMpuPtr > 0.0) {
        stageMpu = *ancestorMpuPtr;
    }

    if (primMpu == stageMpu) {
        return result;  // No conversion needed
    }

    // Apply uniform scaling — both translation and the upper-left 3x3.
    //
    // Meter prim (1.0) in cm stage (0.01): scale = 100× (up)
    // Cm prim (0.01) in meter stage (1.0): scale = 0.01× (down)
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


// ── Registration ────────────────────────────────────────────────────────

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdMetricsGeomMetricsAPI)
{
    // Helper: outputs this prim's metersPerUnit for ancestor traversal
    self.PrimComputation(_tokens->computeMetersPerUnit)
        .Callback<double>(&_ComputeMetersPerUnit)
        .Inputs(
            AttributeValue<double>(_tokens->metersPerUnit)
        );

    // Helper: outputs this prim's upAxis for ancestor traversal
    self.PrimComputation(_tokens->computeUpAxis)
        .Callback<TfToken>(&_ComputeUpAxis)
        .Inputs(
            AttributeValue<TfToken>(_tokens->upAxis)
        );

    // Main computation: corrects L2W for up-axis and scale mismatches
    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareLocalToWorldTransform)
        .Inputs(
            // Standard L2W from execGeom (same prim, cross-schema)
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),

            // metersPerUnit from GeomMetricsAPI (prim attribute)
            AttributeValue<double>(
                _tokens->metersPerUnit),

            // upAxis from GeomMetricsAPI (prim attribute)
            AttributeValue<TfToken>(
                _tokens->upAxis),

            // Nearest ancestor's metersPerUnit
            NamespaceAncestor<double>(
                _tokens->computeMetersPerUnit)
                .InputName(_tokens->ancestorMetersPerUnit),

            // Nearest ancestor's upAxis
            NamespaceAncestor<TfToken>(
                _tokens->computeUpAxis)
                .InputName(_tokens->ancestorUpAxis)
        );
}

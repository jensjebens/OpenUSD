//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usdValidation/usdSolidValidators/validatorTokens.h"
#include "pxr/usdValidation/usdValidation/error.h"
#include "pxr/usdValidation/usdValidation/registry.h"
#include "pxr/usdValidation/usdValidation/validator.h"

#include <iostream>
#include <set>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((usdSolidValidatorsPlugin, "usdSolidValidators"))
);

static bool
_HasError(const UsdValidationErrorVector &errors,
          const std::string &identifierSuffix)
{
    for (const UsdValidationError &error : errors) {
        if (TfStringEndsWith(error.GetIdentifier().GetString(),
                             identifierSuffix)) {
            return true;
        }
    }
    return false;
}

static size_t
_CountError(const UsdValidationErrorVector &errors,
            const std::string &identifierSuffix)
{
    size_t count = 0;
    for (const UsdValidationError &error : errors) {
        if (TfStringEndsWith(error.GetIdentifier().GetString(),
                             identifierSuffix)) {
            ++count;
        }
    }
    return count;
}

// Number of findings a rule produced, counted by the "[BA.xxx]" tag every
// message leads with. Several rules share an error name (the NURBS size rules
// all report NurbControlVertexWeightSizeMismatch), so the tag is what tells
// them apart.
static size_t
_CountRule(const UsdValidationErrorVector &errors, const std::string &rule)
{
    const std::string tag = "[" + rule + "]";
    size_t count = 0;
    for (const UsdValidationError &error : errors) {
        if (TfStringStartsWith(error.GetMessage(), tag)) {
            ++count;
        }
    }
    return count;
}

static UsdStageRefPtr
_OpenLayer(const std::string &contents)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer->ImportFromString(contents));
    UsdStageRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);
    return stage;
}

static const std::string layerContents = R"usda(#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def BrepArray "BadStructure"
    {
        uniform double[] brep:intersectTol3d = [-1.0]
        uniform double3[] brep:extent = [(1, 1, 1), (0, 0, 0)]
        uniform uint[] brep:regionCount = [1]
    }

    def BrepArray "MissingAttrs"
    {
        uniform uint[] brep:regionCount = [1]
    }

    def BrepArray "BadTopology"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [1]
        uniform uint[] region:shellCount = [1]
        uniform token[] region:type = ["solidRegion"]
        # shell arrays should have size 1 (sum of region:shellCount); give 2.
        uniform uint[] shell:faceuseCount = [6, 0]
        uniform uint[] shell:wireEdgeCount = [0, 0]
        uniform token[] shell:pointType = ["none", "none"]
    }

    def BrepArray "BadTokens"
    {
        uniform token[] region:type = ["solidRegion", "bogusRegion"]
        uniform token[] face:surfaceType = ["BrepSurfaceNurbAPI", "NotASurface"]
    }

    def BrepArray "BadRanges"
    {
        uniform uint[] face:loopCount = [1, 0]
        uniform double2[] face:range = [(0, 0), (1, 1), (0, 0), (0, 5)]
        uniform double[] edge:range = [0, 1, 5, 2]
    }

    def BrepArray "BadCylinder"
    {
        uniform token[] face:surfaceType = ["BrepSurfaceCylinderAPI"]
        double3[] brep:surface:cylinder:origin = [(0, 0, 0)]
        double3[] brep:surface:cylinder:axis = [(0, 0, 2)]
        double3[] brep:surface:cylinder:refDirection = [(1, 0, 0)]
        double[] brep:surface:cylinder:radius = [-3.0]
    }

    def BrepArray "GoodCylinder"
    {
        uniform token[] face:surfaceType = ["BrepSurfaceCylinderAPI"]
        double3[] brep:surface:cylinder:origin = [(0, 0, 0)]
        double3[] brep:surface:cylinder:axis = [(0, 0, 1)]
        double3[] brep:surface:cylinder:refDirection = [(1, 0, 0)]
        double[] brep:surface:cylinder:radius = [3.0]
    }

    def BrepArray "MissingFamilies"
    {
        uniform uint[] brep:regionCount = [1]
    }

    def BrepArray "BadRefs"
    {
        uniform uint[] brep:regionCount = [1]
        uniform uint[] region:shellCount = [1]
        uniform token[] region:type = ["solidRegion"]
        uniform uint[] shell:faceuseCount = [2]
        uniform uint[] shell:wireEdgeCount = [0]
        uniform token[] shell:pointType = ["none"]
        uniform uint[] faceuse:faceIndex = [0, 5]
        uniform token[] faceuse:orientationType = ["same", "opposite"]
        uniform uint[] face:loopCount = [1]
        uniform token[] face:surfaceType = ["BrepSurfaceNurbAPI"]
        uniform token[] face:trimType = ["general"]
        uniform double2[] face:range = [(0, 0), (1, 1)]
        uniform uint[] loop:edgeuseCount = [0]
        uniform uint[] loop:vertexIndex = [0]
        uniform token[] edge:curveType = []
        uniform token[] vertex:pointType = ["BrepPointAPI"]
    }

    def BrepArray "BadNurbOrder"
    {
        uniform token[] face:surfaceType = ["BrepSurfaceNurbAPI"]
        uint[] brep:surface:nurb:uOrder = [0]
        uint[] brep:surface:nurb:vOrder = [2]
        uint[] brep:surface:nurb:uVertexCount = [4]
        uint[] brep:surface:nurb:vVertexCount = [4]
    }

    def BrepArray "BadAnalyticCircle"
    {
        uniform token[] edge:curveType = ["BrepCurve3dCircleAPI"]
        point3d[] brep:edge3dCircle:curve3d:circle:center = [(0, 0, 0)]
        vector3d[] brep:edge3dCircle:curve3d:circle:axis = [(0, 0, 1)]
        vector3d[] brep:edge3dCircle:curve3d:circle:refDirection = [(1, 0, 0)]
        double[] brep:edge3dCircle:curve3d:circle:radius = [-1.0]
    }

    # Two NURBS edges with matching curve/vertex directions: edge 0 runs
    # vertex 0 -> vertex 1, edge 1 runs vertex 1 -> vertex 2. Evaluated at
    # their edge:range endpoints, both land on their vertices, so BA.730
    # (BrepArrayEdgeCurveVertices) must NOT flag either.
    def BrepArray "GoodEdgeVertices"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [(0, 0, 0), (1, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:order = [2, 2]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:vertexCount = [2, 2]
        uniform double[] brep:edge3dNurb:curve3d:nurb:weights = [1, 1, 1, 1]
        uniform double[] brep:edge3dNurb:curve3d:nurb:knots = [0, 0, 1, 1, 0, 0, 1, 1]
        uniform token[] edge:curveType = ["BrepCurve3dNurbAPI", "BrepCurve3dNurbAPI"]
        uniform double[] edge:range = [0, 1, 0, 1]
        uniform int2[] edge:vertexIndices = [(0, 1), (1, 2)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI", "BrepPointAPI"]
    }

    # Same geometry as GoodEdgeVertices, but edge 0's vertexIndices are swapped
    # to (1, 0): the curve still runs (0,0,0)->(1,0,0) but the authored order
    # says vertex 1 -> vertex 0. BA.730 must flag edge 0 (and only edge 0).
    def BrepArray "ReversedEdgeVertices"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [(0, 0, 0), (1, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:order = [2, 2]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:vertexCount = [2, 2]
        uniform double[] brep:edge3dNurb:curve3d:nurb:weights = [1, 1, 1, 1]
        uniform double[] brep:edge3dNurb:curve3d:nurb:knots = [0, 0, 1, 1, 0, 0, 1, 1]
        uniform token[] edge:curveType = ["BrepCurve3dNurbAPI", "BrepCurve3dNurbAPI"]
        uniform double[] edge:range = [0, 1, 0, 1]
        uniform int2[] edge:vertexIndices = [(1, 0), (1, 2)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI", "BrepPointAPI"]
    }

    # BA.570 (BrepArraySpans, EdgeRangeSpanExceeded): a periodic circle edge
    # whose parameter span (0 .. 2*pi + 0.1) exceeds one full period. Edge 1 is a
    # healthy quarter-circle. Only edge 0 must be flagged.
    def BrepArray "BadEdgeRangeSpan"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform token[] edge:curveType = ["BrepCurve3dCircleAPI", "BrepCurve3dCircleAPI"]
        uniform double[] edge:range = [0, 6.383185307179586, 0, 1.5707963267948966]
    }

    # The SAME two circle edges authored honestly: edge 0 is a full circle
    # (span exactly 2*pi) and edge 1 a quarter arc. Neither exceeds a period, so
    # BrepArraySpans must NOT flag EdgeRangeSpanExceeded.
    def BrepArray "GoodEdgeRangeSpan"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform token[] edge:curveType = ["BrepCurve3dCircleAPI", "BrepCurve3dCircleAPI"]
        uniform double[] edge:range = [0, 6.283185307179586, 0, 1.5707963267948966]
    }

    # BA.010 (BrepArrayStructure, NonFiniteIntersectTol3d): a non-finite
    # (NaN via 0/0... here authored as inf) intersection tolerance. A non-finite
    # tolerance silently poisons every tolerance-based rule downstream; it is
    # neither "positive" nor "<= 0.0", so the ordering check alone cannot catch
    # it. BrepArrayStructure must flag NonFiniteIntersectTol3d, and must NOT
    # additionally report NonPositiveIntersectTol3d for the same entry.
    def BrepArray "NonFiniteTol"
    {
        uniform double[] brep:intersectTol3d = [inf]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [1]
    }

    # Row 14 (BA.310): brep:extent containment slop must follow the
    # intersectTol3d ladder + a float32-quantization term, not a fixed 1e-11.
    # The box max on X is 1000; the float32 quantization at 1000 is ~6e-5. Vertex
    # 0 sits 5e-5 past the box max (1000.00005): below the float slop, so it must
    # NOT be flagged (it WAS a hard-Error false positive under the old 1e-11).
    # Vertex 1 sits 1.0 past the box (1001): well outside, still flagged.
    def BrepArray "ExtentFloatSlop"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform double3[] brep:extent = [(-1000, -1, -1), (1000, 1, 1)]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(1000.00005, 0, 0), (1001, 0, 0)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI"]
    }

    # Row 16 (BA.532): curve axis frames use the SAME 1e-6 unit-length tolerance
    # as surface frames. Circle 0's axis length is off by 5e-5 (length
    # 1.00005) -> flagged under 1e-6 but NOT under the old 1e-4 curve epsilon.
    # Circle 1 has a unit axis (clean). BrepArrayAnalyticCurves must flag exactly
    # one AnalyticCurveAxisNotUnitLength.
    def BrepArray "CurveFrameTol"
    {
        uniform token[] edge:curveType = ["BrepCurve3dCircleAPI", "BrepCurve3dCircleAPI"]
        point3d[] brep:edge3dCircle:curve3d:circle:center = [(0, 0, 0), (0, 0, 0)]
        vector3d[] brep:edge3dCircle:curve3d:circle:axis = [(0, 0, 1.00005), (0, 0, 1)]
        vector3d[] brep:edge3dCircle:curve3d:circle:refDirection = [(1, 0, 0), (1, 0, 0)]
        double[] brep:edge3dCircle:curve3d:circle:radius = [1.0, 1.0]
    }
}
)usda";

static void
TestRegistration()
{
    const UsdValidationRegistry &registry
        = UsdValidationRegistry::GetInstance();

    const std::set<TfToken> expectedValidatorNames = {
        UsdSolidValidatorNameTokens->brepArrayStructure,
        UsdSolidValidatorNameTokens->brepArrayTopology,
        UsdSolidValidatorNameTokens->brepArrayTokenValues,
        UsdSolidValidatorNameTokens->brepArrayRanges,
        UsdSolidValidatorNameTokens->brepArrayAnalyticSurfaces,
        UsdSolidValidatorNameTokens->brepArrayAuthorship,
        UsdSolidValidatorNameTokens->brepArrayDataTypes,
        UsdSolidValidatorNameTokens->brepArraySchemaUsage,
        UsdSolidValidatorNameTokens->brepArrayReferences,
        UsdSolidValidatorNameTokens->brepArrayCompleteness,
        UsdSolidValidatorNameTokens->brepArrayContainment,
        UsdSolidValidatorNameTokens->brepArraySpans,
        UsdSolidValidatorNameTokens->brepArrayAnalyticCurves,
        UsdSolidValidatorNameTokens->brepArrayNurbs,
        UsdSolidValidatorNameTokens->brepArrayEdgeCurveVertices,
        UsdSolidValidatorNameTokens->brepArrayUvTrim,
        UsdSolidValidatorNameTokens->brepArrayGeomSubsets,
    };

    const UsdValidationValidatorMetadataVector metadata
        = registry.GetValidatorMetadataForPlugin(
            _tokens->usdSolidValidatorsPlugin);
    TF_AXIOM(metadata.size() == 17);

    std::set<TfToken> validatorNames;
    for (const UsdValidationValidatorMetadata &m : metadata) {
        validatorNames.insert(m.name);
    }
    TF_AXIOM(validatorNames == expectedValidatorNames);
}

static void
TestBrepArrayStructure()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayStructure);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/BadStructure"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_HasError(errors, ".NonPositiveIntersectTol3d"));
        TF_AXIOM(_HasError(errors, ".InvalidExtentOrder"));
        // No missing-attribute or size error: all three attrs authored and
        // consistently sized.
        TF_AXIOM(!_HasError(errors, ".MissingBrepAttributes"));
        TF_AXIOM(!_HasError(errors, ".InconsistentBrepArraySizes"));
    }

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/MissingAttrs"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_HasError(errors, ".MissingBrepAttributes"));
        TF_AXIOM(_HasError(errors, ".InconsistentBrepArraySizes"));
    }
}

static void
TestBrepArrayStructureNonFiniteTol()
{
    // BA.010: a non-finite brep:intersectTol3d must be flagged with the
    // NonFiniteIntersectTol3d error (and NOT double-flagged as non-positive),
    // while a valid finite positive tolerance stays clean.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayStructure);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/NonFiniteTol"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_HasError(errors, ".NonFiniteIntersectTol3d"));
        // A non-finite value must not ALSO be reported as non-positive.
        TF_AXIOM(!_HasError(errors, ".NonPositiveIntersectTol3d"));
    }
    {
        // Positive case: GoodCylinder authors no intersectTol3d, and the
        // BadStructure prim's -1.0 tolerance is the non-positive (not
        // non-finite) case, so it must NOT trip the finiteness check.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/BadStructure"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".NonFiniteIntersectTol3d"));
        TF_AXIOM(_HasError(errors, ".NonPositiveIntersectTol3d"));
    }
}

static void
TestBrepArraySpans()
{
    // BA.570/571: a periodic circle/ellipse edge whose parameter span exceeds
    // one period (2*pi) within tolerance must be flagged EdgeRangeSpanExceeded;
    // a full-period (exactly 2*pi) edge must stay clean.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArraySpans);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/BadEdgeRangeSpan"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        // Exactly the over-period edge 0, not the quarter-arc edge 1.
        TF_AXIOM(_CountError(errors, ".EdgeRangeSpanExceeded") == 1);
    }
    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GoodEdgeRangeSpan"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".EdgeRangeSpanExceeded"));
    }
}

static void
TestBrepArrayTopology()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayTopology);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    const UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/BadTopology"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    // shell arrays sized 2 but sum(region:shellCount) == 1.
    TF_AXIOM(_HasError(errors, ".InconsistentShellArraySizes"));
}

static void
TestBrepArrayTokenValues()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayTokenValues);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    const UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/BadTokens"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_CountError(errors, ".InvalidRegionType") == 1);
    TF_AXIOM(_CountError(errors, ".InvalidFaceSurfaceType") == 1);
}

static void
TestBrepArrayRanges()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayRanges);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    const UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/BadRanges"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_HasError(errors, ".InvalidFaceLoopCount"));
    TF_AXIOM(_HasError(errors, ".DegenerateFaceURange"));
    TF_AXIOM(_HasError(errors, ".InvalidEdgeRangeOrder"));
}

static void
TestBrepArrayAnalyticSurfaces()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayAnalyticSurfaces);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/BadCylinder"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_HasError(errors, ".NonUnitSurfaceAxis"));
        TF_AXIOM(_HasError(errors, ".NonPositiveSurfaceRadius"));
    }

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GoodCylinder"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(errors.empty());
    }
}

static void
TestBrepArrayAuthorship()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayAuthorship);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim
        = stage->GetPrimAtPath(SdfPath("/World/MissingFamilies"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    // MissingFamilies authors only brep:regionCount, so the always-required
    // region and shell families (region:type, region:shellCount, shell:*) are
    // unauthored even under lenient family gating.
    TF_AXIOM(_HasError(errors, ".AttributeNotAuthored"));
}

static void
TestBrepArrayReferences()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayReferences);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/BadRefs"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    // faceuse:faceIndex value 5 is outside the single Brep's one-face range.
    TF_AXIOM(_HasError(errors, ".FaceuseFaceIndexOutOfRange"));
}

static void
TestBrepArrayNurbs()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayNurbs);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/BadNurbOrder"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_HasError(errors, ".NurbNonPositiveOrder"));
}

static void
TestBrepArrayAnalyticCurves()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayAnalyticCurves);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim
        = stage->GetPrimAtPath(SdfPath("/World/BadAnalyticCircle"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_HasError(errors, ".AnalyticCurveNonPositiveRadius"));
}

static void
TestBrepArrayEdgeCurveVertices()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayEdgeCurveVertices);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        // Curve directions agree with vertexIndices order: no findings.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GoodEdgeVertices"));
        TF_AXIOM(prim);
        TF_AXIOM(_CountRule(validator->Validate(prim), "BA.730") == 0);
    }

    {
        // Edge 0's vertexIndices are swapped against its curve direction, so
        // its start point lands on the wrong vertex. One finding per edge.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/ReversedEdgeVertices"));
        TF_AXIOM(prim);
        TF_AXIOM(_CountRule(validator->Validate(prim), "BA.730") == 1);
    }
}

static void
TestBrepArrayContainmentFloatSlop()
{
    // Row 14 (BA.310): the brep:extent containment slop follows the
    // intersectTol3d ladder plus a float32-quantization term, not a fixed
    // 1e-11. A vertex 5e-5 past a box max of 1000 (below the ~6e-5 float slop at
    // that magnitude) must NOT be flagged -- under the old 1e-11 slop it was a
    // hard-Error false positive. A vertex 1.0 past the box is still flagged.
    // Exactly one vertexPositionOutsideBrepExtent must fire.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayContainment);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim
        = stage->GetPrimAtPath(SdfPath("/World/ExtentFloatSlop"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    // Only the vertex well outside the box (vertex 1) is flagged; the 5e-5
    // overshoot (vertex 0) is within the float-quantization slop.
    TF_AXIOM(_CountError(errors, ".VertexPositionOutsideBrepExtent") == 1);
}

static void
TestBrepArrayCurveFrameTol()
{
    // Row 16 (BA.532): curve axis frames use the same 1e-6 unit-length
    // tolerance as surface frames. A circle axis of length 1.00005 (off by
    // 5e-5) is flagged under 1e-6 but was cleared under the old 1e-4 curve
    // epsilon; a unit axis stays clean. Exactly one
    // AnalyticCurveAxisNotUnitLength must fire.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayAnalyticCurves);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim
        = stage->GetPrimAtPath(SdfPath("/World/CurveFrameTol"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_CountError(errors, ".AnalyticCurveAxisNotUnitLength") == 1);
}

static const std::string countsAndSubsetsContents = R"usda(#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Scope "Materials"
    {
        def Material "Red"
        {
        }
    }

    def BrepArray "CountsBelowMinimum"
    {
        uniform double[] brep:intersectTol3d = [0.000001]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [0]
        uniform uint[] region:shellCount = [0]
        uniform uint[] shell:faceuseCount = [0]
        uniform uint[] shell:wireEdgeCount = [0]
        uniform token[] shell:pointType = ["none"]
    }

    def BrepArray "PointPositionSizes"
    {
        uniform double[] brep:intersectTol3d = [0.000001]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [1]
        uniform uint[] region:shellCount = [1]
        uniform uint[] shell:faceuseCount = [2]
        uniform uint[] shell:wireEdgeCount = [0]
        uniform token[] shell:pointType = ["BrepPointAPI"]
        uniform int2[] edge:vertexIndices = [(0, 1), (1, 2), (2, 3), (3, 0)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI", "BrepPointAPI"]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0)]
    }

    def BrepArray "WireEdgeRangeStructure"
    {
        uniform double[] brep:intersectTol3d = [0.000001]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [1]
        uniform uint[] region:shellCount = [1]
        uniform uint[] shell:faceuseCount = [2]
        uniform uint[] shell:wireEdgeCount = [1]
        uniform token[] shell:pointType = ["none"]
        uniform double[] wireEdge:range = [0, 1, 0]
    }

    def BrepArray "GeomSubsetsBad"
    {
        uniform double[] brep:intersectTol3d = [0.000001]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [1]
        uniform token[] face:surfaceType = ["BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI"]

        def GeomSubset "outOfRange"
        {
            uniform token elementType = "face"
            int[] indices = [0, 7]
        }

        def GeomSubset "overlapA"
        {
            uniform token elementType = "face"
            int[] indices = [1]
        }

        def GeomSubset "overlapB"
        {
            uniform token elementType = "face"
            int[] indices = [1]
        }

        def GeomSubset "danglingMaterial"
        {
            uniform token elementType = "face"
            int[] indices = [2]
            rel material:binding = </World/Materials/Missing>
        }
    }

    def BrepArray "GeomSubsetsGood"
    {
        uniform double[] brep:intersectTol3d = [0.000001]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform uint[] brep:regionCount = [1]
        uniform token[] face:surfaceType = ["BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI"]

        def GeomSubset "facesA"
        {
            uniform token elementType = "face"
            int[] indices = [0, 1]
            rel material:binding = </World/Materials/Red>
        }

        def GeomSubset "facesB"
        {
            uniform token elementType = "face"
            int[] indices = [2]
            rel material:binding = </World/Materials/Red>
        }

        def GeomSubset "wholeBrep"
        {
            uniform token elementType = "brep"
            int[] indices = [0]
            rel material:binding = </World/Materials/Red>
        }
    }
}
)usda";

static void
TestBrepArrayMinimumCountsAndSizes()
{
    // BA.270 / BA.295 / BA.320 / BA.325 / BA.700 / BA.701 / BA.702.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayStructure);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(countsAndSubsetsContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/CountsBelowMinimum"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".RegionCountBelowMinimum") == 1);
        TF_AXIOM(_CountError(errors, ".RegionShellCountBelowMinimum") == 1);
        TF_AXIOM(_CountError(errors, ".ShellWithoutContent") == 1);
    }

    {
        // vertex:pointType holds 3 entries where edge:vertexIndices reaches
        // vertex 3, and the position array holds 2 of the 3 BrepPointAPI
        // vertices. The shell's BrepPointAPI token sits on a face shell
        // (faceuseCount 2), where it is ignored, so no shellPoint position is
        // expected of it.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/PointPositionSizes"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".VertexArraySizeMismatch") == 1);
        TF_AXIOM(_CountError(errors, ".VertexPointPositionSizeMismatch") == 1);
        TF_AXIOM(_CountError(errors, ".ShellPointPositionSizeMismatch") == 0);
        TF_AXIOM(!_HasError(errors, ".RegionCountBelowMinimum"));
        TF_AXIOM(!_HasError(errors, ".ShellWithoutContent"));
    }

    {
        // One wire edge asks for a 2-element wireEdge:range; 3 are authored.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/WireEdgeRangeStructure"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".InvalidWireEdgeRangeStructure") == 1);
    }
}

static void
TestBrepArrayGeomSubsets()
{
    // BA.680 / BA.681 / BA.682.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayGeomSubsets);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(countsAndSubsetsContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GeomSubsetsBad"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".GeomSubsetIndexOutOfRange") == 1);
        TF_AXIOM(_CountError(errors, ".GeomSubsetIndicesOverlap") == 1);
        TF_AXIOM(
            _CountError(errors, ".GeomSubsetMaterialBindingTargetMissing")
            == 1);
    }

    {
        // Two disjoint face subsets and one brep subset, all bound to a
        // material that is on the stage.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GeomSubsetsGood"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(errors.empty());
    }
}

// OMPE-106532: shell:pointType names a point only on a shell with no faceuses
// and no wire edges. The cases mirror test_brep_validator.py's
// test_ignored_shell_point_tokens_do_not_create_geometry_occurrences,
// test_true_point_shell_requires_exactly_one_position_occurrence and
// test_ignored_shell_point_token_does_not_shift_later_brep_position_span.
static const std::string pointShellContents = R"usda(#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def BrepArray "FaceShellPointToken"
    {
        uniform token[] shell:pointType = ["BrepPointAPI"]
        uniform uint[] shell:faceuseCount = [1]
        uniform uint[] shell:wireEdgeCount = [0]
    }

    def BrepArray "WireShellPointToken"
    {
        uniform token[] shell:pointType = ["BrepPointAPI"]
        uniform uint[] shell:faceuseCount = [0]
        uniform uint[] shell:wireEdgeCount = [1]
    }

    def BrepArray "TruePointShell"
    {
        uniform token[] shell:pointType = ["BrepPointAPI"]
        uniform uint[] shell:faceuseCount = [0]
        uniform uint[] shell:wireEdgeCount = [0]
    }

    def BrepArray "TruePointShellWithPosition"
    {
        uniform token[] shell:pointType = ["BrepPointAPI"]
        uniform uint[] shell:faceuseCount = [0]
        uniform uint[] shell:wireEdgeCount = [0]
        uniform point3d[] brep:shellPoint:point:position = [(1, 2, 3)]
    }

    def BrepArray "IgnoredTokenInside"
    {
        uniform uint[] brep:regionCount = [1, 1]
        uniform uint[] region:shellCount = [1, 1]
        uniform uint[] shell:faceuseCount = [1, 0]
        uniform uint[] shell:wireEdgeCount = [0, 0]
        uniform token[] shell:pointType = ["BrepPointAPI", "BrepPointAPI"]
        uniform double3[] brep:extent = [(-1, -1, -1), (1, 1, 1), (9, 9, 9), (11, 11, 11)]
        uniform point3d[] brep:shellPoint:point:position = [(10, 10, 10)]
    }

    def BrepArray "IgnoredTokenOutside"
    {
        uniform uint[] brep:regionCount = [1, 1]
        uniform uint[] region:shellCount = [1, 1]
        uniform uint[] shell:faceuseCount = [1, 0]
        uniform uint[] shell:wireEdgeCount = [0, 0]
        uniform token[] shell:pointType = ["BrepPointAPI", "BrepPointAPI"]
        uniform double3[] brep:extent = [(-1, -1, -1), (1, 1, 1), (9, 9, 9), (11, 11, 11)]
        uniform point3d[] brep:shellPoint:point:position = [(12, 10, 10)]
    }
}
)usda";

static void
TestBrepArrayPointShells()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *structure
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArrayStructure);
    const UsdValidationValidator *schemaUsage
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArraySchemaUsage);
    const UsdValidationValidator *containment
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArrayContainment);
    TF_AXIOM(structure && schemaUsage && containment);

    UsdStageRefPtr stage = _OpenLayer(pointShellContents);
    const auto prim = [&](const char *name) {
        const UsdPrim p = stage->GetPrimAtPath(
            SdfPath("/World").AppendChild(TfToken(name)));
        TF_AXIOM(p);
        return p;
    };

    // A BrepPointAPI token on a face or a wire shell is ignored: it asks for
    // no shellPoint position and no BrepPointAPI:shellPoint.
    for (const char *name : { "FaceShellPointToken", "WireShellPointToken" }) {
        TF_AXIOM(_CountRule(structure->Validate(prim(name)), "BA.325") == 0);
        TF_AXIOM(_CountRule(schemaUsage->Validate(prim(name)), "BA.583")
                 == 0);
    }

    // A true point shell asks for exactly one position, and for the API.
    TF_AXIOM(_CountRule(structure->Validate(prim("TruePointShell")), "BA.325")
             == 1);
    TF_AXIOM(_CountRule(schemaUsage->Validate(prim("TruePointShell")),
                        "BA.583") == 1);
    TF_AXIOM(_CountRule(structure->Validate(
                            prim("TruePointShellWithPosition")), "BA.325")
             == 0);

    // Brep 0's face shell carries an ignored token, so the one position
    // belongs to Brep 1's point shell and is measured against Brep 1's box.
    TF_AXIOM(_CountRule(structure->Validate(prim("IgnoredTokenInside")),
                        "BA.325") == 0);
    TF_AXIOM(_CountRule(containment->Validate(prim("IgnoredTokenInside")),
                        "BA.710") == 0);
    const UsdValidationErrorVector outside
        = containment->Validate(prim("IgnoredTokenOutside"));
    TF_AXIOM(_CountRule(outside, "BA.710") == 1);
    for (const UsdValidationError &error : outside) {
        if (TfStringStartsWith(error.GetMessage(), "[BA.710]")) {
            TF_AXIOM(TfStringContains(error.GetMessage(), "brep #1"));
        }
    }
}

// A BrepArray with one UV pcurve per vertexCount entry (order 2, or the 0/0
// "no pcurve" sentinel), and the weights given -- or none at all when
// `weights` is null. Mirrors _make_curve_uv_prim in test_brep_validator.py.
static std::string
_UvCurvePrimUsda(const std::string &name,
                 const std::vector<unsigned int> &vertexCounts,
                 const std::vector<double> *weights)
{
    std::vector<std::string> edgeuses, orders, counts, cvs, knots;
    size_t cvTotal = 0;
    for (size_t i = 0; i < vertexCounts.size(); ++i) {
        const unsigned int vc = vertexCounts[i];
        const unsigned int order = vc == 0 ? 0u : 2u;
        edgeuses.push_back(TfStringify(i));
        orders.push_back(TfStringify(order));
        counts.push_back(TfStringify(vc));
        for (unsigned int k = 0; k < order + vc; ++k) {
            knots.push_back(TfStringify(k));
        }
        cvTotal += vc;
    }
    for (size_t c = 0; c < cvTotal; ++c) {
        cvs.push_back(TfStringPrintf("(%zu, 0)", c));
    }
    std::string text = TfStringPrintf(
        "    def BrepArray \"%s\" (\n"
        "        prepend apiSchemas = [\"BrepCurveUvNurbAPI\"]\n"
        "    )\n"
        "    {\n"
        "        uniform uint[] edgeuse:edgeIndex = [%s]\n"
        "        uniform uint[] brep:curveUv:nurb:order = [%s]\n"
        "        uniform uint[] brep:curveUv:nurb:vertexCount = [%s]\n"
        "        uniform double2[] brep:curveUv:nurb:controlVertices = [%s]\n"
        "        uniform double[] brep:curveUv:nurb:knots = [%s]\n",
        name.c_str(), TfStringJoin(edgeuses, ", ").c_str(),
        TfStringJoin(orders, ", ").c_str(), TfStringJoin(counts, ", ").c_str(),
        TfStringJoin(cvs, ", ").c_str(), TfStringJoin(knots, ", ").c_str());
    if (weights) {
        std::vector<std::string> w;
        for (const double v : *weights) {
            w.push_back(TfStringify(v));
        }
        text += TfStringPrintf(
            "        uniform double[] brep:curveUv:nurb:weights = [%s]\n",
            TfStringJoin(w, ", ").c_str());
    }
    return text + "    }\n";
}

static void
TestBrepArrayUvWeightCardinality()
{
    // OMPE-106502: BA.405 compares the packed weights against the packed UV
    // control-vertex total whether or not weights are authored. The cases are
    // test_uv_curve_weights_match_packed_control_vertex_count's.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayNurbs);
    TF_AXIOM(validator);

    struct Case {
        const char *name;
        std::vector<unsigned int> vertexCounts;
        bool authored;
        std::vector<double> weights;
        size_t expected;
    };
    const std::vector<Case> cases = {
        { "Missing", { 2 }, false, {}, 1 },
        { "AuthoredEmpty", { 2 }, true, {}, 1 },
        { "Short", { 2 }, true, { 1.0 }, 1 },
        { "Exact", { 2 }, true, { 1.0, 1.0 }, 0 },
        { "Long", { 2 }, true, { 1.0, 1.0, 1.0 }, 1 },
        { "PackedShort", { 2, 3 }, true, { 1, 1, 1, 1 }, 1 },
        { "PackedExact", { 2, 3 }, true, { 1, 1, 1, 1, 1 }, 0 },
        { "PackedLong", { 2, 3 }, true, { 1, 1, 1, 1, 1, 1 }, 1 },
        { "MissingSentinelWeights", { 0, 0 }, false, {}, 0 },
        { "MixedSentinel", { 2, 0 }, true, { 1.0, 1.0 }, 0 },
        // Positivity (BA.410) is independent of cardinality.
        { "NonPositive", { 2 }, true, { 0.0, 1.0 }, 0 },
    };

    std::string contents = "#usda 1.0\ndef Xform \"World\"\n{\n";
    for (const Case &c : cases) {
        contents += _UvCurvePrimUsda(c.name, c.vertexCounts,
                                     c.authored ? &c.weights : nullptr);
    }
    contents += "}\n";
    UsdStageRefPtr stage = _OpenLayer(contents);

    for (const Case &c : cases) {
        const UsdPrim prim = stage->GetPrimAtPath(
            SdfPath("/World").AppendChild(TfToken(c.name)));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountRule(errors, "BA.405") == c.expected);
    }
    const UsdPrim nonPositive
        = stage->GetPrimAtPath(SdfPath("/World/NonPositive"));
    TF_AXIOM(_CountRule(validator->Validate(nonPositive), "BA.410") == 1);
}

static const std::string typesAndSeveritiesContents = R"usda(#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def BrepArray "VectorPositions"
    {
        uniform vector3d[] brep:vertexPoint:point:position = [(0, 0, 0)]
        uniform vector3d[] brep:shellPoint:point:position = [(0, 0, 0)]
    }

    def BrepArray "IntRegionShellCount"
    {
        uniform int[] region:shellCount = [1]
    }

    def BrepArray "PointPositions"
    {
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0)]
        uniform point3d[] brep:shellPoint:point:position = [(0, 0, 0)]
    }

    def BrepArray "ControlVerticesOutside"
    {
        uniform uint[] brep:regionCount = [1]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 1)]
        uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [(5, 5, 5)]
        uniform point3d[] brep:surface:nurb:controlVertices = [(5, 5, 5)]
    }

    def BrepArray "FullPeriodCylinderNoSeam"
    {
        uniform token[] face:surfaceType = ["BrepSurfaceCylinderAPI"]
        uniform double2[] face:range = [(0, 0), (6.283185307179586, 1)]
        uniform uint[] face:loopCount = [1]
        uniform uint[] loop:edgeuseCount = [2]
        uniform uint[] edgeuse:edgeIndex = [0, 1]
    }
}
)usda";

// The rule's findings, all of which must carry `type`.
static size_t
_CountRuleWithType(const UsdValidationErrorVector &errors,
                   const std::string &rule, UsdValidationErrorType type)
{
    size_t count = 0;
    for (const UsdValidationError &error : errors) {
        if (TfStringStartsWith(error.GetMessage(), "[" + rule + "]")) {
            TF_AXIOM(error.GetType() == type);
            ++count;
        }
    }
    return count;
}

static void
TestBrepArrayPositionTypesAndSeverities()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *dataTypes
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArrayDataTypes);
    const UsdValidationValidator *containment
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArrayContainment);
    const UsdValidationValidator *uvTrim = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayUvTrim);
    TF_AXIOM(dataTypes && containment && uvTrim);

    UsdStageRefPtr stage = _OpenLayer(typesAndSeveritiesContents);

    // BA.326 / BA.327: the positions are point3d[], exactly. vector3d[] holds
    // the same GfVec3d values and still fails, as it does in Python.
    {
        const UsdValidationErrorVector errors = dataTypes->Validate(
            stage->GetPrimAtPath(SdfPath("/World/VectorPositions")));
        TF_AXIOM(_CountRule(errors, "BA.326") == 1);
        TF_AXIOM(_CountRule(errors, "BA.327") == 1);
    }
    {
        const UsdValidationErrorVector errors = dataTypes->Validate(
            stage->GetPrimAtPath(SdfPath("/World/PointPositions")));
        TF_AXIOM(_CountRule(errors, "BA.326") == 0);
        TF_AXIOM(_CountRule(errors, "BA.327") == 0);
    }
    {
        // int[] where the schema declares uint[].
        const UsdValidationErrorVector errors = dataTypes->Validate(
            stage->GetPrimAtPath(SdfPath("/World/IntRegionShellCount")));
        TF_AXIOM(_CountRule(errors, "BA.076") == 1);
    }

    // BA.365 / BA.465 and BA.761 are failed checks in brep_validator.py, so
    // Errors here; only BA.702 is a Python warning.
    {
        const UsdValidationErrorVector errors = containment->Validate(
            stage->GetPrimAtPath(SdfPath("/World/ControlVerticesOutside")));
        TF_AXIOM(_CountRuleWithType(errors, "BA.365",
                                    UsdValidationErrorType::Error) == 1);
        TF_AXIOM(_CountRuleWithType(errors, "BA.465",
                                    UsdValidationErrorType::Error) == 1);
    }
    {
        const UsdValidationErrorVector errors = uvTrim->Validate(
            stage->GetPrimAtPath(SdfPath("/World/FullPeriodCylinderNoSeam")));
        TF_AXIOM(_CountRuleWithType(errors, "BA.761",
                                    UsdValidationErrorType::Error) == 1);
    }
}

static const std::string presenceContents = R"usda(#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def BrepArray "NothingAuthored"
    {
    }

    def BrepArray "DeclaredWithoutValues"
    {
        uniform double[] brep:intersectTol3d
        uniform double3[] brep:extent
        uniform uint[] brep:regionCount
        uniform uint[] region:shellCount
        uniform token[] region:type
        uniform uint[] shell:faceuseCount
        uniform uint[] shell:wireEdgeCount
        uniform token[] shell:pointType
        uniform uint[] faceuse:faceIndex
        uniform token[] faceuse:orientationType
        uniform uint[] face:loopCount
        uniform token[] face:trimType
        uniform token[] face:surfaceType
        uniform double2[] face:range
        uniform uint[] loop:edgeuseCount
        uniform uint[] loop:vertexIndex
        uniform token[] edge:curveType
        uniform int2[] edge:vertexIndices
        uniform double[] edge:range
        uniform uint[] edgeuse:edgeIndex
        uniform token[] edgeuse:orientationType
        uniform uint[] edgeuse:nextRadialEUIndex
        uniform token[] edgeuse:thisRadialEntryType
        uniform token[] vertex:pointType
    }

    def BrepArray "PartialWireEdges"
    {
        uniform token[] wireEdge:curveType = []
    }

    def BrepArray "WireEdgesDeclaredNotAuthored"
    {
        uniform uint[] shell:wireEdgeCount = [1]
    }
}
)usda";

static void
TestBrepArrayPresence()
{
    // brep_validator.py counts an attribute as present when it has any
    // authored opinion, checks every topology family whether or not it has
    // members, and reports each missing attribute on its own.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *authorship
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArrayAuthorship);
    const UsdValidationValidator *structure
        = registry.GetOrLoadValidatorByName(
            UsdSolidValidatorNameTokens->brepArrayStructure);
    TF_AXIOM(authorship && structure);

    UsdStageRefPtr stage = _OpenLayer(presenceContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/NothingAuthored"));
        const UsdValidationErrorVector errors = authorship->Validate(prim);
        TF_AXIOM(_CountRule(errors, "BA.070") == 2);
        TF_AXIOM(_CountRule(errors, "BA.085") == 3);
        TF_AXIOM(_CountRule(errors, "BA.105") == 2);
        TF_AXIOM(_CountRule(errors, "BA.125") == 4);
        TF_AXIOM(_CountRule(errors, "BA.170") == 2);
        TF_AXIOM(_CountRule(errors, "BA.215") == 3);
        TF_AXIOM(_CountRule(errors, "BA.185") == 4);
        TF_AXIOM(_CountRule(errors, "BA.300") == 1);
        // No wire edges declared and none authored: nothing to report.
        TF_AXIOM(_CountRule(errors, "BA.255") == 0);
        TF_AXIOM(_CountRule(structure->Validate(prim), "BA.005") == 3);
    }
    {
        // A declaration without a value is an authored opinion.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/DeclaredWithoutValues"));
        TF_AXIOM(!_HasError(authorship->Validate(prim),
                            ".AttributeNotAuthored"));
        TF_AXIOM(_CountRule(structure->Validate(prim), "BA.005") == 0);
    }
    {
        // Some wire-edge attributes but not all: one finding naming both.
        const UsdValidationErrorVector errors = authorship->Validate(
            stage->GetPrimAtPath(SdfPath("/World/PartialWireEdges")));
        TF_AXIOM(_CountRule(errors, "BA.255") == 1);
    }
    {
        // Wire edges declared, none of the three authored: one per attribute.
        const UsdValidationErrorVector errors = authorship->Validate(
            stage->GetPrimAtPath(
                SdfPath("/World/WireEdgesDeclaredNotAuthored")));
        TF_AXIOM(_CountRule(errors, "BA.255") == 3);
    }
}

int
main()
{
    TestRegistration();
    TestBrepArrayStructure();
    TestBrepArrayStructureNonFiniteTol();
    TestBrepArraySpans();
    TestBrepArrayTopology();
    TestBrepArrayTokenValues();
    TestBrepArrayRanges();
    TestBrepArrayAnalyticSurfaces();
    TestBrepArrayAuthorship();
    TestBrepArrayReferences();
    TestBrepArrayNurbs();
    TestBrepArrayAnalyticCurves();
    TestBrepArrayEdgeCurveVertices();
    TestBrepArrayContainmentFloatSlop();
    TestBrepArrayCurveFrameTol();
    TestBrepArrayMinimumCountsAndSizes();
    TestBrepArrayGeomSubsets();
    TestBrepArrayPointShells();
    TestBrepArrayUvWeightCardinality();
    TestBrepArrayPositionTypesAndSeverities();
    TestBrepArrayPresence();

    std::cout << "OK\n";
    return EXIT_SUCCESS;
}

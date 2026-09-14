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

    # A face whose FIRST (outer) loop has zero edgeuses: an edgeless periodic
    # surface (a cylinder/sphere/cone authored as a single seamless NURBS
    # patch). Proposal #109 rule 424/425 requires the outer loop to carry seam
    # edges, so BrepArrayFaceOuterLoop must flag FaceOuterLoopNoEdges.
    def BrepArray "BadOuterLoop"
    {
        uniform uint[] face:loopCount = [1]
        uniform uint[] loop:edgeuseCount = [0]
        uniform uint[] loop:vertexIndex = [0]
    }

    # A face authored honestly: its FIRST (outer) loop carries four edgeuses,
    # and it has a SECOND (inner) loop that is a legal degenerate vertex-loop
    # (zero edgeuses, a vertexIndex). Rule 428 permits a zero-edgeuse loop only
    # as a non-first inner loop, so BrepArrayFaceOuterLoop must NOT flag it.
    def BrepArray "GoodOuterLoopWithInnerVertex"
    {
        uniform uint[] face:loopCount = [2]
        uniform uint[] loop:edgeuseCount = [4, 0]
        uniform uint[] loop:vertexIndex = [0, 1]
        uniform uint[] edgeuse:edgeIndex = [0, 1, 2, 3]
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

    # A single planar patch with four single-use (identity-ring) boundary edges
    # that LABELS itself a closed solid (regionCount = [void, solid]). The solid
    # shell is not closed -> BrepArraySolidClosure must flag SolidShellOpenEdge.
    def BrepArray "OpenSolidPatch"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 0)]
        uniform uint[] brep:regionCount = [2]
        uniform uint[] region:shellCount = [1, 1]
        uniform token[] region:type = ["voidRegion", "solidRegion"]
        uniform uint[] shell:faceuseCount = [1, 1]
        uniform uint[] shell:wireEdgeCount = [0, 0]
        uniform token[] shell:pointType = ["none", "none"]
        uniform uint[] faceuse:faceIndex = [0, 0]
        uniform token[] faceuse:orientationType = ["same", "opposite"]
        uniform uint[] face:loopCount = [1]
        uniform token[] face:surfaceType = ["BrepSurfacePlaneAPI"]
        uniform token[] face:trimType = ["general"]
        uniform double2[] face:range = [(0, 0), (1, 1)]
        uniform uint[] loop:edgeuseCount = [4]
        uniform uint[] loop:vertexIndex = [0]
        uniform uint[] edgeuse:edgeIndex = [0, 1, 2, 3]
        uniform token[] edgeuse:orientationType = ["same", "same", "same", "same"]
        uniform uint[] edgeuse:nextRadialEUIndex = [0, 1, 2, 3]
        uniform token[] edgeuse:thisRadialEntryType = ["topEntry", "topEntry", "topEntry", "topEntry"]
        uniform token[] edge:curveType = ["BrepCurve3dLineAPI", "BrepCurve3dLineAPI", "BrepCurve3dLineAPI", "BrepCurve3dLineAPI"]
        uniform int2[] edge:vertexIndices = [(0, 1), (1, 2), (2, 3), (3, 0)]
        uniform double[] edge:range = [0, 1, 0, 1, 0, 1, 0, 1]
        uniform token[] vertex:pointType = ["none", "none", "none", "none"]
    }

    # The SAME single open patch authored honestly as a sheet: one voidRegion,
    # no solidRegion. Its single-use boundary edges are legal on a sheet, so
    # BrepArraySolidClosure must NOT flag it (valid sheets pass).
    def BrepArray "OpenSheetPatch"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform double3[] brep:extent = [(0, 0, 0), (1, 1, 0)]
        uniform uint[] brep:regionCount = [1]
        uniform uint[] region:shellCount = [1]
        uniform token[] region:type = ["voidRegion"]
        uniform uint[] shell:faceuseCount = [2]
        uniform uint[] shell:wireEdgeCount = [0]
        uniform token[] shell:pointType = ["none"]
        uniform uint[] faceuse:faceIndex = [0, 0]
        uniform token[] faceuse:orientationType = ["same", "opposite"]
        uniform uint[] face:loopCount = [1]
        uniform token[] face:surfaceType = ["BrepSurfacePlaneAPI"]
        uniform token[] face:trimType = ["general"]
        uniform double2[] face:range = [(0, 0), (1, 1)]
        uniform uint[] loop:edgeuseCount = [4]
        uniform uint[] loop:vertexIndex = [0]
        uniform uint[] edgeuse:edgeIndex = [0, 1, 2, 3]
        uniform token[] edgeuse:orientationType = ["same", "same", "same", "same"]
        uniform uint[] edgeuse:nextRadialEUIndex = [0, 1, 2, 3]
        uniform token[] edgeuse:thisRadialEntryType = ["topEntry", "topEntry", "topEntry", "topEntry"]
        uniform token[] edge:curveType = ["BrepCurve3dLineAPI", "BrepCurve3dLineAPI", "BrepCurve3dLineAPI", "BrepCurve3dLineAPI"]
        uniform int2[] edge:vertexIndices = [(0, 1), (1, 2), (2, 3), (3, 0)]
        uniform double[] edge:range = [0, 1, 0, 1, 0, 1, 0, 1]
        uniform token[] vertex:pointType = ["none", "none", "none", "none"]
    }

    # Two NURBS edges. Edge 0 has all control vertices equal (both (0,0,0)) and
    # its vertexIndices name the same vertex twice: a degenerate/collapsed apex
    # edge that proposal rule 381 forbids. Edge 1 is a healthy line from
    # vertex 1 to vertex 2. BrepArrayDegenerateEdges must flag exactly edge 0.
    def BrepArray "DegenerateEdges"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [(0, 0, 0), (0, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:order = [2, 2]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:vertexCount = [2, 2]
        uniform double[] brep:edge3dNurb:curve3d:nurb:weights = [1, 1, 1, 1]
        uniform token[] edge:curveType = ["BrepCurve3dNurbAPI", "BrepCurve3dNurbAPI"]
        uniform double[] edge:range = [0, 1, 0, 1]
        uniform int2[] edge:vertexIndices = [(0, 0), (1, 2)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI", "BrepPointAPI"]
    }

    # A degenerate analytic (zero-length line) edge: origin == vertex, direction
    # scaled by a zero-length edge:range span. BrepArrayDegenerateEdges must flag
    # it via the analytic (zero arc length) path.
    def BrepArray "DegenerateAnalyticEdge" (
        prepend apiSchemas = ["BrepCurve3dLineAPI:edge3dLine"]
    )
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0)]
        uniform vector3d[] brep:edge3dLine:curve3d:line:direction = [(1, 0, 0), (1, 0, 0)]
        uniform point3d[] brep:edge3dLine:curve3d:line:origin = [(0, 0, 0), (0, 0, 0)]
        uniform token[] edge:curveType = ["BrepCurve3dLineAPI", "BrepCurve3dLineAPI"]
        # Edge 0 span 5->5 = 0 (degenerate); edge 1 span 0->1 = healthy.
        uniform double[] edge:range = [5, 5, 0, 1]
        uniform int2[] edge:vertexIndices = [(0, 0), (0, 1)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI"]
    }

    # Two NURBS edges with matching curve/vertex directions: edge 0 runs
    # vertex 0 -> vertex 1, edge 1 runs vertex 1 -> vertex 2, and the control
    # hulls agree. BrepArrayEdgeCurveVertices must NOT flag either.
    def BrepArray "GoodEdgeVertices"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [(0, 0, 0), (1, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:order = [2, 2]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:vertexCount = [2, 2]
        uniform double[] brep:edge3dNurb:curve3d:nurb:weights = [1, 1, 1, 1]
        uniform token[] edge:curveType = ["BrepCurve3dNurbAPI", "BrepCurve3dNurbAPI"]
        uniform double[] edge:range = [0, 1, 0, 1]
        uniform int2[] edge:vertexIndices = [(0, 1), (1, 2)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI", "BrepPointAPI"]
    }

    # Same geometry as GoodEdgeVertices, but edge 0's vertexIndices are swapped
    # to (1, 0): the curve still runs (0,0,0)->(1,0,0) but the authored order
    # says vertex 1 -> vertex 0. This is exactly the topological-order authoring
    # rule 434 forbids. BrepArrayEdgeCurveVertices must flag edge 0 (and only 0).
    def BrepArray "ReversedEdgeVertices"
    {
        uniform double[] brep:intersectTol3d = [1e-6]
        uniform uint[] brep:regionCount = [1]
        uniform point3d[] brep:vertexPoint:point:position = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [(0, 0, 0), (1, 0, 0), (1, 0, 0), (2, 0, 0)]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:order = [2, 2]
        uniform uint[] brep:edge3dNurb:curve3d:nurb:vertexCount = [2, 2]
        uniform double[] brep:edge3dNurb:curve3d:nurb:weights = [1, 1, 1, 1]
        uniform token[] edge:curveType = ["BrepCurve3dNurbAPI", "BrepCurve3dNurbAPI"]
        uniform double[] edge:range = [0, 1, 0, 1]
        uniform int2[] edge:vertexIndices = [(1, 0), (1, 2)]
        uniform token[] vertex:pointType = ["BrepPointAPI", "BrepPointAPI", "BrepPointAPI"]
    }

    # BA.061 (BrepArrayDataTypes): analytic geometry attributes authored with
    # the wrong value type. A production STEP->UsdSolid conversion authored
    # analytic axes/positions with wrong precision/role and scalar parameters
    # with the wrong scalar type; those "type" mistakes previously produced no
    # data-type diagnostic. Here the cylinder axis is a float3[] (not a
    # double-precision 3-vector) and the radius is a float[] (not double[]).
    # BrepArrayDataTypes must flag both.
    def BrepArray "BadAnalyticTypes"
    {
        uniform token[] face:surfaceType = ["BrepSurfaceCylinderAPI"]
        uniform point3d[] brep:surface:cylinder:origin = [(0, 0, 0)]
        uniform float3[] brep:surface:cylinder:axis = [(0, 0, 1)]
        uniform vector3d[] brep:surface:cylinder:refDirection = [(1, 0, 0)]
        uniform float[] brep:surface:cylinder:radius = [3.0]
    }

    # The SAME cylinder authored with conformant types, including the
    # point3d/vector3d role-aliases the lenient policy accepts. BrepArrayDataTypes
    # must NOT flag any of these.
    def BrepArray "GoodAnalyticTypes"
    {
        uniform token[] face:surfaceType = ["BrepSurfaceCylinderAPI"]
        uniform point3d[] brep:surface:cylinder:origin = [(0, 0, 0)]
        uniform vector3d[] brep:surface:cylinder:axis = [(0, 0, 1)]
        uniform vector3d[] brep:surface:cylinder:refDirection = [(1, 0, 0)]
        uniform double[] brep:surface:cylinder:radius = [3.0]
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

    # Row 13 (BA.230): the analytic degeneracy branch must measure arc length
    # against the Brep's brep:intersectTol3d, not a fixed 1e-4 curve epsilon.
    # intersectTol3d is 0.01 here. Edge 0 is a circle of radius 1 spanning
    # 0.005 rad -> arc length 0.005 <= 0.01, so it is degenerate under the
    # authored tolerance (it was NOT flagged under the old fixed 1e-4 epsilon,
    # since 0.005 > 1e-4). Edge 1 is a healthy quarter arc (arc length ~1.57).
    # Edge 2 is an eccentric ellipse (xRadius 10, yRadius 0.001) spanning
    # 0.005 rad: its upper-bound arc length uses max(xr, yr) = 10 -> 0.05 > 0.01,
    # so max(xr,yr) keeps it NON-degenerate (the old mean-radius form would have
    # used ~5.0 and also cleared it, but max is the conservative bound). Only
    # edge 0 must be flagged.
    def BrepArray "DegenerateAnalyticArcTol"
    {
        uniform double[] brep:intersectTol3d = [0.01]
        uniform uint[] brep:regionCount = [1]
        uniform token[] edge:curveType = ["BrepCurve3dCircleAPI", "BrepCurve3dCircleAPI", "BrepCurve3dEllipseAPI"]
        point3d[] brep:edge3dCircle:curve3d:circle:center = [(0, 0, 0), (0, 0, 0)]
        vector3d[] brep:edge3dCircle:curve3d:circle:axis = [(0, 0, 1), (0, 0, 1)]
        vector3d[] brep:edge3dCircle:curve3d:circle:refDirection = [(1, 0, 0), (1, 0, 0)]
        double[] brep:edge3dCircle:curve3d:circle:radius = [1.0, 1.0]
        point3d[] brep:edge3dEllipse:curve3d:ellipse:center = [(0, 0, 0)]
        vector3d[] brep:edge3dEllipse:curve3d:ellipse:axis = [(0, 0, 1)]
        vector3d[] brep:edge3dEllipse:curve3d:ellipse:refDirection = [(1, 0, 0)]
        double[] brep:edge3dEllipse:curve3d:ellipse:xRadius = [10.0]
        double[] brep:edge3dEllipse:curve3d:ellipse:yRadius = [0.001]
        # Edge 0: 0.005 rad span (degenerate). Edge 1: quarter arc (healthy).
        # Edge 2: ellipse 0.005 rad span (non-degenerate via max(xr,yr)).
        uniform double[] edge:range = [0, 0.005, 0, 1.5707963267948966, 0, 0.005]
    }

    # Row 15 (BA.230): a BrepArray that authors NO brep:intersectTol3d exercises
    # the reader-side fallback (_FallbackIntersectTol3d = 1e-6). Edge 0 is a
    # zero-direction-scaled line spanning 5e-7 model units -> arc length 5e-7,
    # which is <= 1e-6 (flagged) but was > 1e-9 under the OLD fallback (NOT
    # flagged). Edge 1 spans 1e-3 (healthy under either fallback). Exactly edge 0
    # must be flagged, and only because the fallback is 1e-6.
    def BrepArray "UnauthoredTolDegenerate"
    {
        uniform uint[] brep:regionCount = [1]
        uniform token[] edge:curveType = ["BrepCurve3dLineAPI", "BrepCurve3dLineAPI"]
        point3d[] brep:edge3dLine:curve3d:line:origin = [(0, 0, 0), (0, 0, 0)]
        vector3d[] brep:edge3dLine:curve3d:line:direction = [(1, 0, 0), (1, 0, 0)]
        # Edge 0 span 0 -> 5e-7 (arc 5e-7, sub-fallback). Edge 1 span 0 -> 1e-3.
        uniform double[] edge:range = [0, 5e-7, 0, 1e-3]
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
        UsdSolidValidatorNameTokens->brepArrayFaceOuterLoop,
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
        UsdSolidValidatorNameTokens->brepArraySolidClosure,
        UsdSolidValidatorNameTokens->brepArrayDegenerateEdges,
        UsdSolidValidatorNameTokens->brepArrayEdgeCurveVertices,
        UsdSolidValidatorNameTokens->brepArrayUvTrim,
        UsdSolidValidatorNameTokens->brepArrayGeomSubsets,
    };

    const UsdValidationValidatorMetadataVector metadata
        = registry.GetValidatorMetadataForPlugin(
            _tokens->usdSolidValidatorsPlugin);
    TF_AXIOM(metadata.size() == 20);

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
TestBrepArrayDataTypes()
{
    // BA.061: analytic geometry attribute types. A wrong-precision axis
    // (float3[]) and a wrong scalar radius (float[]) must be flagged with
    // InvalidAttributeDataType; the conformant cylinder (using the accepted
    // point3d/vector3d role-aliases and double[]) must stay clean.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayDataTypes);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/BadAnalyticTypes"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        // The float3[] axis and the float[] radius are both wrong.
        TF_AXIOM(_CountError(errors, ".InvalidAttributeDataType") == 2);
    }
    {
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GoodAnalyticTypes"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".InvalidAttributeDataType"));
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
TestBrepArrayFaceOuterLoop()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayFaceOuterLoop);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        // Face whose first/outer loop has zero edgeuses: an edgeless periodic
        // surface. Rule 424/425 requires the outer loop to carry seam edges.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/BadOuterLoop"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".FaceOuterLoopNoEdges") == 1);
    }

    {
        // Outer loop carries four edgeuses; the zero-edgeuse loop is a legal
        // degenerate inner (non-first) vertex-loop (rule 428) and must NOT be
        // flagged.
        const UsdPrim prim = stage->GetPrimAtPath(
            SdfPath("/World/GoodOuterLoopWithInnerVertex"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".FaceOuterLoopNoEdges"));
    }
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
TestBrepArraySolidClosure()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArraySolidClosure);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        // A single open patch labeled solidRegion: every boundary edge is
        // single-use, so the solid shell is not closed.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/OpenSolidPatch"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_HasError(errors, ".SolidShellOpenEdge"));
    }

    {
        // The same open patch authored honestly as a voidRegion-only sheet:
        // single-use edges are legal on a sheet, so it must NOT be flagged.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/OpenSheetPatch"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".SolidShellOpenEdge"));
        TF_AXIOM(errors.empty());
    }
}

static void
TestBrepArrayDegenerateEdges()
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayDegenerateEdges);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);

    {
        // Edge 0 has all NURBS control vertices equal; edge 1 is healthy.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/DegenerateEdges"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".DegenerateEdge") == 1);
    }

    {
        // Edge 0 is a zero-length (zero arc) analytic line; edge 1 is healthy.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/DegenerateAnalyticEdge"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".DegenerateEdge") == 1);
    }

    {
        // Clean, non-degenerate edges: no findings.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/GoodEdgeVertices"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".DegenerateEdge"));
    }
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
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(!_HasError(errors, ".EdgeCurveVertexMismatch"));
    }

    {
        // Edge 0's vertexIndices are swapped versus its curve direction
        // (topological rather than curve-parametric order). Rule 434 flags it.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/ReversedEdgeVertices"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".EdgeCurveVertexMismatch") == 1);
    }
}

static void
TestBrepArrayDegenerateAnalyticArcTol()
{
    // Row 13 (BA.230): the analytic degeneracy branch measures arc length
    // against the Brep's brep:intersectTol3d (here 0.01), not a fixed 1e-4
    // curve epsilon. A 0.005-rad circle arc (arc length 0.005 <= 0.01) is
    // degenerate under the authored tolerance; the old 1e-4 epsilon cleared it.
    // A quarter arc is healthy, and an eccentric ellipse's upper-bound arc
    // length uses max(xRadius, yRadius) so it is not falsely collapsed. Exactly
    // one edge (edge 0) must be flagged.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayDegenerateEdges);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim
        = stage->GetPrimAtPath(SdfPath("/World/DegenerateAnalyticArcTol"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_CountError(errors, ".DegenerateEdge") == 1);
}

static void
TestBrepArrayUnauthoredTolFallback()
{
    // Row 15 (BA.230): with no authored brep:intersectTol3d, the reader-side
    // fallback (_FallbackIntersectTol3d = 1e-6) governs degeneracy. A 5e-7-long
    // analytic line edge is <= 1e-6 (degenerate) but was > 1e-9 under the old
    // fallback (clean). Exactly the sub-fallback edge 0 must be flagged, which
    // proves the fallback magnitude is 1e-6 rather than the former 1e-9.
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();
    const UsdValidationValidator *validator = registry.GetOrLoadValidatorByName(
        UsdSolidValidatorNameTokens->brepArrayDegenerateEdges);
    TF_AXIOM(validator);

    UsdStageRefPtr stage = _OpenLayer(layerContents);
    const UsdPrim prim
        = stage->GetPrimAtPath(SdfPath("/World/UnauthoredTolDegenerate"));
    TF_AXIOM(prim);
    const UsdValidationErrorVector errors = validator->Validate(prim);
    TF_AXIOM(_CountError(errors, ".DegenerateEdge") == 1);
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
        // vertex 3, the position array holds 2 of the 3 BrepPointAPI vertices,
        // and the BrepPointAPI shell has no shellPoint position at all.
        const UsdPrim prim
            = stage->GetPrimAtPath(SdfPath("/World/PointPositionSizes"));
        TF_AXIOM(prim);
        const UsdValidationErrorVector errors = validator->Validate(prim);
        TF_AXIOM(_CountError(errors, ".VertexArraySizeMismatch") == 1);
        TF_AXIOM(_CountError(errors, ".VertexPointPositionSizeMismatch") == 1);
        TF_AXIOM(_CountError(errors, ".ShellPointPositionSizeMismatch") == 1);
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

int
main()
{
    TestRegistration();
    TestBrepArrayStructure();
    TestBrepArrayStructureNonFiniteTol();
    TestBrepArrayDataTypes();
    TestBrepArraySpans();
    TestBrepArrayTopology();
    TestBrepArrayTokenValues();
    TestBrepArrayRanges();
    TestBrepArrayFaceOuterLoop();
    TestBrepArrayAnalyticSurfaces();
    TestBrepArrayAuthorship();
    TestBrepArrayReferences();
    TestBrepArrayNurbs();
    TestBrepArrayAnalyticCurves();
    TestBrepArraySolidClosure();
    TestBrepArrayDegenerateEdges();
    TestBrepArrayEdgeCurveVertices();
    TestBrepArrayDegenerateAnalyticArcTol();
    TestBrepArrayUnauthoredTolFallback();
    TestBrepArrayContainmentFloatSlop();
    TestBrepArrayCurveFrameTol();
    TestBrepArrayMinimumCountsAndSizes();
    TestBrepArrayGeomSubsets();

    std::cout << "OK\n";
    return EXIT_SUCCESS;
}

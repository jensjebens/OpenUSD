//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_TOKENS_H
#define USDSOLID_TOKENS_H

/// \file usdSolid/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include ".//api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class UsdSolidTokensType
///
/// \link UsdSolidTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdSolidTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdSolidTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdSolidTokens->bottomEntry);
/// \endcode
struct UsdSolidTokensType {
    USDSOLID_API UsdSolidTokensType();
    /// \brief "bottomEntry"
    /// 
    /// Possible value for UsdSolidBrepArray::GetEdgeuseThisRadialEntryTypeAttr()
    const TfToken bottomEntry;
    /// \brief "brep"
    /// 
    /// Property namespace prefix for the UsdSolidBrepPointAPI schema., Property namespace prefix for the UsdSolidBrepCurve3dNurbAPI schema., Property namespace prefix for the UsdSolidBrepCurve3dLineAPI schema., Property namespace prefix for the UsdSolidBrepCurve3dCircleAPI schema., Property namespace prefix for the UsdSolidBrepCurve3dEllipseAPI schema.
    const TfToken brep;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:circle:axis"
    /// 
    /// UsdSolidBrepCurve3dCircleAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dCircleAxis;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:circle:center"
    /// 
    /// UsdSolidBrepCurve3dCircleAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dCircleCenter;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:circle:radius"
    /// 
    /// UsdSolidBrepCurve3dCircleAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dCircleRadius;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:circle:refDirection"
    /// 
    /// UsdSolidBrepCurve3dCircleAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dCircleRefDirection;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:ellipse:axis"
    /// 
    /// UsdSolidBrepCurve3dEllipseAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dEllipseAxis;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:ellipse:center"
    /// 
    /// UsdSolidBrepCurve3dEllipseAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dEllipseCenter;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:ellipse:refDirection"
    /// 
    /// UsdSolidBrepCurve3dEllipseAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dEllipseRefDirection;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:ellipse:xRadius"
    /// 
    /// UsdSolidBrepCurve3dEllipseAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dEllipseXRadius;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:ellipse:yRadius"
    /// 
    /// UsdSolidBrepCurve3dEllipseAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dEllipseYRadius;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:line:direction"
    /// 
    /// UsdSolidBrepCurve3dLineAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dLineDirection;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:line:origin"
    /// 
    /// UsdSolidBrepCurve3dLineAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dLineOrigin;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:nurb:controlVertices"
    /// 
    /// UsdSolidBrepCurve3dNurbAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dNurbControlVertices;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:nurb:knots"
    /// 
    /// UsdSolidBrepCurve3dNurbAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dNurbKnots;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:nurb:order"
    /// 
    /// UsdSolidBrepCurve3dNurbAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dNurbOrder;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:nurb:vertexCount"
    /// 
    /// UsdSolidBrepCurve3dNurbAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dNurbVertexCount;
    /// \brief "brep:__INSTANCE_NAME__:curve3d:nurb:weights"
    /// 
    /// UsdSolidBrepCurve3dNurbAPI
    const TfToken brep_MultipleApplyTemplate_Curve3dNurbWeights;
    /// \brief "brep:__INSTANCE_NAME__:point:position"
    /// 
    /// UsdSolidBrepPointAPI
    const TfToken brep_MultipleApplyTemplate_PointPosition;
    /// \brief "brep:curveUv:nurb:controlVertices"
    /// 
    /// UsdSolidBrepCurveUvNurbAPI
    const TfToken brepCurveUvNurbControlVertices;
    /// \brief "brep:curveUv:nurb:knots"
    /// 
    /// UsdSolidBrepCurveUvNurbAPI
    const TfToken brepCurveUvNurbKnots;
    /// \brief "brep:curveUv:nurb:order"
    /// 
    /// UsdSolidBrepCurveUvNurbAPI
    const TfToken brepCurveUvNurbOrder;
    /// \brief "brep:curveUv:nurb:vertexCount"
    /// 
    /// UsdSolidBrepCurveUvNurbAPI
    const TfToken brepCurveUvNurbVertexCount;
    /// \brief "brep:curveUv:nurb:weights"
    /// 
    /// UsdSolidBrepCurveUvNurbAPI
    const TfToken brepCurveUvNurbWeights;
    /// \brief "brep:extent"
    /// 
    /// UsdSolidBrepArray
    const TfToken brepExtent;
    /// \brief "brep:intersectTol3d"
    /// 
    /// UsdSolidBrepArray
    const TfToken brepIntersectTol3d;
    /// \brief "brep:regionCount"
    /// 
    /// UsdSolidBrepArray
    const TfToken brepRegionCount;
    /// \brief "brep:surface:cone:axis"
    /// 
    /// UsdSolidBrepSurfaceConeAPI
    const TfToken brepSurfaceConeAxis;
    /// \brief "brep:surface:cone:origin"
    /// 
    /// UsdSolidBrepSurfaceConeAPI
    const TfToken brepSurfaceConeOrigin;
    /// \brief "brep:surface:cone:radius"
    /// 
    /// UsdSolidBrepSurfaceConeAPI
    const TfToken brepSurfaceConeRadius;
    /// \brief "brep:surface:cone:refDirection"
    /// 
    /// UsdSolidBrepSurfaceConeAPI
    const TfToken brepSurfaceConeRefDirection;
    /// \brief "brep:surface:cone:semiAngle"
    /// 
    /// UsdSolidBrepSurfaceConeAPI
    const TfToken brepSurfaceConeSemiAngle;
    /// \brief "brep:surface:cylinder:axis"
    /// 
    /// UsdSolidBrepSurfaceCylinderAPI
    const TfToken brepSurfaceCylinderAxis;
    /// \brief "brep:surface:cylinder:origin"
    /// 
    /// UsdSolidBrepSurfaceCylinderAPI
    const TfToken brepSurfaceCylinderOrigin;
    /// \brief "brep:surface:cylinder:radius"
    /// 
    /// UsdSolidBrepSurfaceCylinderAPI
    const TfToken brepSurfaceCylinderRadius;
    /// \brief "brep:surface:cylinder:refDirection"
    /// 
    /// UsdSolidBrepSurfaceCylinderAPI
    const TfToken brepSurfaceCylinderRefDirection;
    /// \brief "brep:surface:nurb:controlVertices"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbControlVertices;
    /// \brief "brep:surface:nurb:uKnots"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbUKnots;
    /// \brief "brep:surface:nurb:uOrder"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbUOrder;
    /// \brief "brep:surface:nurb:uVertexCount"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbUVertexCount;
    /// \brief "brep:surface:nurb:vKnots"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbVKnots;
    /// \brief "brep:surface:nurb:vOrder"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbVOrder;
    /// \brief "brep:surface:nurb:vVertexCount"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbVVertexCount;
    /// \brief "brep:surface:nurb:weights"
    /// 
    /// UsdSolidBrepSurfaceNurbAPI
    const TfToken brepSurfaceNurbWeights;
    /// \brief "brep:surface:plane:axis"
    /// 
    /// UsdSolidBrepSurfacePlaneAPI
    const TfToken brepSurfacePlaneAxis;
    /// \brief "brep:surface:plane:origin"
    /// 
    /// UsdSolidBrepSurfacePlaneAPI
    const TfToken brepSurfacePlaneOrigin;
    /// \brief "brep:surface:plane:refDirection"
    /// 
    /// UsdSolidBrepSurfacePlaneAPI
    const TfToken brepSurfacePlaneRefDirection;
    /// \brief "brep:surface:sphere:axis"
    /// 
    /// UsdSolidBrepSurfaceSphereAPI
    const TfToken brepSurfaceSphereAxis;
    /// \brief "brep:surface:sphere:center"
    /// 
    /// UsdSolidBrepSurfaceSphereAPI
    const TfToken brepSurfaceSphereCenter;
    /// \brief "brep:surface:sphere:radius"
    /// 
    /// UsdSolidBrepSurfaceSphereAPI
    const TfToken brepSurfaceSphereRadius;
    /// \brief "brep:surface:sphere:refDirection"
    /// 
    /// UsdSolidBrepSurfaceSphereAPI
    const TfToken brepSurfaceSphereRefDirection;
    /// \brief "brep:surface:torus:axis"
    /// 
    /// UsdSolidBrepSurfaceTorusAPI
    const TfToken brepSurfaceTorusAxis;
    /// \brief "brep:surface:torus:majorRadius"
    /// 
    /// UsdSolidBrepSurfaceTorusAPI
    const TfToken brepSurfaceTorusMajorRadius;
    /// \brief "brep:surface:torus:minorRadius"
    /// 
    /// UsdSolidBrepSurfaceTorusAPI
    const TfToken brepSurfaceTorusMinorRadius;
    /// \brief "brep:surface:torus:origin"
    /// 
    /// UsdSolidBrepSurfaceTorusAPI
    const TfToken brepSurfaceTorusOrigin;
    /// \brief "brep:surface:torus:refDirection"
    /// 
    /// UsdSolidBrepSurfaceTorusAPI
    const TfToken brepSurfaceTorusRefDirection;
    /// \brief "edge:curveType"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeCurveType;
    /// \brief "edge:range"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeRange;
    /// \brief "edgeuse:edgeIndex"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeuseEdgeIndex;
    /// \brief "edgeuse:nextRadialEUIndex"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeuseNextRadialEUIndex;
    /// \brief "edgeuse:orientationType"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeuseOrientationType;
    /// \brief "edgeuse:thisRadialEntryType"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeuseThisRadialEntryType;
    /// \brief "edge:vertexIndices"
    /// 
    /// UsdSolidBrepArray
    const TfToken edgeVertexIndices;
    /// \brief "face:loopCount"
    /// 
    /// UsdSolidBrepArray
    const TfToken faceLoopCount;
    /// \brief "face:range"
    /// 
    /// UsdSolidBrepArray
    const TfToken faceRange;
    /// \brief "face:surfaceType"
    /// 
    /// UsdSolidBrepArray
    const TfToken faceSurfaceType;
    /// \brief "face:trimType"
    /// 
    /// UsdSolidBrepArray
    const TfToken faceTrimType;
    /// \brief "faceuse:faceIndex"
    /// 
    /// UsdSolidBrepArray
    const TfToken faceuseFaceIndex;
    /// \brief "faceuse:orientationType"
    /// 
    /// UsdSolidBrepArray
    const TfToken faceuseOrientationType;
    /// \brief "general"
    /// 
    /// Possible value for UsdSolidBrepArray::GetFaceTrimTypeAttr()
    const TfToken general;
    /// \brief "loop:edgeuseCount"
    /// 
    /// UsdSolidBrepArray
    const TfToken loopEdgeuseCount;
    /// \brief "loop:vertexIndex"
    /// 
    /// UsdSolidBrepArray
    const TfToken loopVertexIndex;
    /// \brief "none"
    /// 
    /// Possible value for UsdSolidBrepArray::GetShellPointTypeAttr()
    const TfToken none;
    /// \brief "opposite"
    /// 
    /// Possible value for UsdSolidBrepArray::GetEdgeuseOrientationTypeAttr(), Possible value for UsdSolidBrepArray::GetFaceuseOrientationTypeAttr()
    const TfToken opposite;
    /// \brief "rectangular"
    /// 
    /// Possible value for UsdSolidBrepArray::GetFaceTrimTypeAttr()
    const TfToken rectangular;
    /// \brief "region:shellCount"
    /// 
    /// UsdSolidBrepArray
    const TfToken regionShellCount;
    /// \brief "region:type"
    /// 
    /// UsdSolidBrepArray
    const TfToken regionType;
    /// \brief "same"
    /// 
    /// Possible value for UsdSolidBrepArray::GetEdgeuseOrientationTypeAttr(), Possible value for UsdSolidBrepArray::GetFaceuseOrientationTypeAttr()
    const TfToken same;
    /// \brief "shell:faceuseCount"
    /// 
    /// UsdSolidBrepArray
    const TfToken shellFaceuseCount;
    /// \brief "shell:pointType"
    /// 
    /// UsdSolidBrepArray
    const TfToken shellPointType;
    /// \brief "shell:wireEdgeCount"
    /// 
    /// UsdSolidBrepArray
    const TfToken shellWireEdgeCount;
    /// \brief "solidRegion"
    /// 
    /// Possible value for UsdSolidBrepArray::GetRegionTypeAttr()
    const TfToken solidRegion;
    /// \brief "topEntry"
    /// 
    /// Possible value for UsdSolidBrepArray::GetEdgeuseThisRadialEntryTypeAttr()
    const TfToken topEntry;
    /// \brief "vertex:pointType"
    /// 
    /// UsdSolidBrepArray
    const TfToken vertexPointType;
    /// \brief "voidRegion"
    /// 
    /// Possible value for UsdSolidBrepArray::GetRegionTypeAttr()
    const TfToken voidRegion;
    /// \brief "wireEdge:curveType"
    /// 
    /// UsdSolidBrepArray
    const TfToken wireEdgeCurveType;
    /// \brief "wireEdge:range"
    /// 
    /// UsdSolidBrepArray
    const TfToken wireEdgeRange;
    /// \brief "wireEdge:vertexIndices"
    /// 
    /// UsdSolidBrepArray
    const TfToken wireEdgeVertexIndices;
    /// \brief "BrepArray"
    /// 
    /// Schema identifer and family for UsdSolidBrepArray
    const TfToken BrepArray;
    /// \brief "BrepCurve3dCircleAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepCurve3dCircleAPI, Possible value for UsdSolidBrepArray::GetEdgeCurveTypeAttr(), Possible value for UsdSolidBrepArray::GetWireEdgeCurveTypeAttr()
    const TfToken BrepCurve3dCircleAPI;
    /// \brief "BrepCurve3dEllipseAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepCurve3dEllipseAPI, Possible value for UsdSolidBrepArray::GetEdgeCurveTypeAttr(), Possible value for UsdSolidBrepArray::GetWireEdgeCurveTypeAttr()
    const TfToken BrepCurve3dEllipseAPI;
    /// \brief "BrepCurve3dLineAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepCurve3dLineAPI, Possible value for UsdSolidBrepArray::GetEdgeCurveTypeAttr(), Possible value for UsdSolidBrepArray::GetWireEdgeCurveTypeAttr()
    const TfToken BrepCurve3dLineAPI;
    /// \brief "BrepCurve3dNurbAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepCurve3dNurbAPI, Possible value for UsdSolidBrepArray::GetEdgeCurveTypeAttr(), Possible value for UsdSolidBrepArray::GetWireEdgeCurveTypeAttr()
    const TfToken BrepCurve3dNurbAPI;
    /// \brief "BrepCurveUvNurbAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepCurveUvNurbAPI
    const TfToken BrepCurveUvNurbAPI;
    /// \brief "BrepPointAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepPointAPI, Possible value for UsdSolidBrepArray::GetShellPointTypeAttr(), Possible value for UsdSolidBrepArray::GetVertexPointTypeAttr()
    const TfToken BrepPointAPI;
    /// \brief "BrepSurfaceConeAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepSurfaceConeAPI, Possible value for UsdSolidBrepArray::GetFaceSurfaceTypeAttr()
    const TfToken BrepSurfaceConeAPI;
    /// \brief "BrepSurfaceCylinderAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepSurfaceCylinderAPI, Possible value for UsdSolidBrepArray::GetFaceSurfaceTypeAttr()
    const TfToken BrepSurfaceCylinderAPI;
    /// \brief "BrepSurfaceNurbAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepSurfaceNurbAPI, Possible value for UsdSolidBrepArray::GetFaceSurfaceTypeAttr()
    const TfToken BrepSurfaceNurbAPI;
    /// \brief "BrepSurfacePlaneAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepSurfacePlaneAPI, Possible value for UsdSolidBrepArray::GetFaceSurfaceTypeAttr()
    const TfToken BrepSurfacePlaneAPI;
    /// \brief "BrepSurfaceSphereAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepSurfaceSphereAPI, Possible value for UsdSolidBrepArray::GetFaceSurfaceTypeAttr()
    const TfToken BrepSurfaceSphereAPI;
    /// \brief "BrepSurfaceTorusAPI"
    /// 
    /// Schema identifer and family for UsdSolidBrepSurfaceTorusAPI, Possible value for UsdSolidBrepArray::GetFaceSurfaceTypeAttr()
    const TfToken BrepSurfaceTorusAPI;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdSolidTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdSolidTokensType
extern USDSOLID_API TfStaticData<UsdSolidTokensType> UsdSolidTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif

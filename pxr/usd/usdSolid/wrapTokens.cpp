//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// GENERATED FILE.  DO NOT EDIT.
#include "pxr/external/boost/python/class.hpp"
#include ".//tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define _ADD_TOKEN(cls, name) \
    cls.add_static_property(#name, +[]() { return UsdSolidTokens->name.GetString(); });

void wrapUsdSolidTokens()
{
    pxr_boost::python::class_<UsdSolidTokensType, pxr_boost::python::noncopyable>
        cls("Tokens", pxr_boost::python::no_init);
    _ADD_TOKEN(cls, bottomEntry);
    _ADD_TOKEN(cls, brep);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dCircleAxis);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dCircleCenter);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dCircleRadius);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dCircleRefDirection);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dEllipseAxis);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dEllipseCenter);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dEllipseRefDirection);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dEllipseXRadius);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dEllipseYRadius);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dLineDirection);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dLineOrigin);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dNurbControlVertices);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dNurbKnots);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dNurbOrder);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dNurbVertexCount);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_Curve3dNurbWeights);
    _ADD_TOKEN(cls, brep_MultipleApplyTemplate_PointPosition);
    _ADD_TOKEN(cls, brepCurveUvNurbControlVertices);
    _ADD_TOKEN(cls, brepCurveUvNurbKnots);
    _ADD_TOKEN(cls, brepCurveUvNurbOrder);
    _ADD_TOKEN(cls, brepCurveUvNurbVertexCount);
    _ADD_TOKEN(cls, brepCurveUvNurbWeights);
    _ADD_TOKEN(cls, brepExtent);
    _ADD_TOKEN(cls, brepIntersectTol3d);
    _ADD_TOKEN(cls, brepRegionCount);
    _ADD_TOKEN(cls, brepSurfaceConeAxis);
    _ADD_TOKEN(cls, brepSurfaceConeOrigin);
    _ADD_TOKEN(cls, brepSurfaceConeRadius);
    _ADD_TOKEN(cls, brepSurfaceConeRefDirection);
    _ADD_TOKEN(cls, brepSurfaceConeSemiAngle);
    _ADD_TOKEN(cls, brepSurfaceCylinderAxis);
    _ADD_TOKEN(cls, brepSurfaceCylinderOrigin);
    _ADD_TOKEN(cls, brepSurfaceCylinderRadius);
    _ADD_TOKEN(cls, brepSurfaceCylinderRefDirection);
    _ADD_TOKEN(cls, brepSurfaceNurbControlVertices);
    _ADD_TOKEN(cls, brepSurfaceNurbUKnots);
    _ADD_TOKEN(cls, brepSurfaceNurbUOrder);
    _ADD_TOKEN(cls, brepSurfaceNurbUVertexCount);
    _ADD_TOKEN(cls, brepSurfaceNurbVKnots);
    _ADD_TOKEN(cls, brepSurfaceNurbVOrder);
    _ADD_TOKEN(cls, brepSurfaceNurbVVertexCount);
    _ADD_TOKEN(cls, brepSurfaceNurbWeights);
    _ADD_TOKEN(cls, brepSurfacePlaneAxis);
    _ADD_TOKEN(cls, brepSurfacePlaneOrigin);
    _ADD_TOKEN(cls, brepSurfacePlaneRefDirection);
    _ADD_TOKEN(cls, brepSurfaceSphereAxis);
    _ADD_TOKEN(cls, brepSurfaceSphereCenter);
    _ADD_TOKEN(cls, brepSurfaceSphereRadius);
    _ADD_TOKEN(cls, brepSurfaceSphereRefDirection);
    _ADD_TOKEN(cls, brepSurfaceTorusAxis);
    _ADD_TOKEN(cls, brepSurfaceTorusMajorRadius);
    _ADD_TOKEN(cls, brepSurfaceTorusMinorRadius);
    _ADD_TOKEN(cls, brepSurfaceTorusOrigin);
    _ADD_TOKEN(cls, brepSurfaceTorusRefDirection);
    _ADD_TOKEN(cls, edgeCurveType);
    _ADD_TOKEN(cls, edgeRange);
    _ADD_TOKEN(cls, edgeuseEdgeIndex);
    _ADD_TOKEN(cls, edgeuseNextRadialEUIndex);
    _ADD_TOKEN(cls, edgeuseOrientationType);
    _ADD_TOKEN(cls, edgeuseThisRadialEntryType);
    _ADD_TOKEN(cls, edgeVertexIndices);
    _ADD_TOKEN(cls, faceLoopCount);
    _ADD_TOKEN(cls, faceRange);
    _ADD_TOKEN(cls, faceSurfaceType);
    _ADD_TOKEN(cls, faceTrimType);
    _ADD_TOKEN(cls, faceuseFaceIndex);
    _ADD_TOKEN(cls, faceuseOrientationType);
    _ADD_TOKEN(cls, general);
    _ADD_TOKEN(cls, loopEdgeuseCount);
    _ADD_TOKEN(cls, loopVertexIndex);
    _ADD_TOKEN(cls, none);
    _ADD_TOKEN(cls, opposite);
    _ADD_TOKEN(cls, rectangular);
    _ADD_TOKEN(cls, regionShellCount);
    _ADD_TOKEN(cls, regionType);
    _ADD_TOKEN(cls, same);
    _ADD_TOKEN(cls, shellFaceuseCount);
    _ADD_TOKEN(cls, shellPointType);
    _ADD_TOKEN(cls, shellWireEdgeCount);
    _ADD_TOKEN(cls, solidRegion);
    _ADD_TOKEN(cls, topEntry);
    _ADD_TOKEN(cls, vertexPointType);
    _ADD_TOKEN(cls, voidRegion);
    _ADD_TOKEN(cls, wireEdgeCurveType);
    _ADD_TOKEN(cls, wireEdgeRange);
    _ADD_TOKEN(cls, wireEdgeVertexIndices);
    _ADD_TOKEN(cls, BrepArray);
    _ADD_TOKEN(cls, BrepCurve3dCircleAPI);
    _ADD_TOKEN(cls, BrepCurve3dEllipseAPI);
    _ADD_TOKEN(cls, BrepCurve3dLineAPI);
    _ADD_TOKEN(cls, BrepCurve3dNurbAPI);
    _ADD_TOKEN(cls, BrepCurveUvNurbAPI);
    _ADD_TOKEN(cls, BrepPointAPI);
    _ADD_TOKEN(cls, BrepSurfaceConeAPI);
    _ADD_TOKEN(cls, BrepSurfaceCylinderAPI);
    _ADD_TOKEN(cls, BrepSurfaceNurbAPI);
    _ADD_TOKEN(cls, BrepSurfacePlaneAPI);
    _ADD_TOKEN(cls, BrepSurfaceSphereAPI);
    _ADD_TOKEN(cls, BrepSurfaceTorusAPI);
}

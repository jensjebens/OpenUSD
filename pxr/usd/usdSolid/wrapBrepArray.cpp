//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepArray.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/wrapTypeHelpers.h"

#include "pxr/external/boost/python.hpp"

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

#define WRAP_CUSTOM                                                     \
    template <class Cls> static void _CustomWrapCode(Cls &_class)

// fwd decl.
WRAP_CUSTOM;

        
static UsdAttribute
_CreateBrepIntersectTol3dAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBrepIntersectTol3dAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateBrepExtentAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBrepExtentAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Double3Array), writeSparsely);
}
        
static UsdAttribute
_CreateBrepRegionCountAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBrepRegionCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateRegionShellCountAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateRegionShellCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateRegionTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateRegionTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateShellFaceuseCountAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateShellFaceuseCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateShellWireEdgeCountAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateShellWireEdgeCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateShellPointTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateShellPointTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateFaceuseFaceIndexAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFaceuseFaceIndexAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateFaceuseOrientationTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFaceuseOrientationTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateFaceLoopCountAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFaceLoopCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateFaceSurfaceTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFaceSurfaceTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateFaceTrimTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFaceTrimTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateFaceRangeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFaceRangeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Double2Array), writeSparsely);
}
        
static UsdAttribute
_CreateLoopEdgeuseCountAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateLoopEdgeuseCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateLoopVertexIndexAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateLoopVertexIndexAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeuseEdgeIndexAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeuseEdgeIndexAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeuseOrientationTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeuseOrientationTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeuseNextRadialEUIndexAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeuseNextRadialEUIndexAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeuseThisRadialEntryTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeuseThisRadialEntryTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeCurveTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeCurveTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeRangeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeRangeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateEdgeVertexIndicesAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEdgeVertexIndicesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int2Array), writeSparsely);
}
        
static UsdAttribute
_CreateWireEdgeCurveTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateWireEdgeCurveTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}
        
static UsdAttribute
_CreateWireEdgeRangeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateWireEdgeRangeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateWireEdgeVertexIndicesAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateWireEdgeVertexIndicesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int2Array), writeSparsely);
}
        
static UsdAttribute
_CreateVertexPointTypeAttr(UsdSolidBrepArray &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateVertexPointTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->TokenArray), writeSparsely);
}

static std::string
_Repr(const UsdSolidBrepArray &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdSolid.BrepArray(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapUsdSolidBrepArray()
{
    typedef UsdSolidBrepArray This;

    class_<This, bases<UsdGeomGprim> >
        cls("BrepArray");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("Define", &This::Define, (arg("stage"), arg("path")))
        .staticmethod("Define")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetBrepIntersectTol3dAttr",
             &This::GetBrepIntersectTol3dAttr)
        .def("CreateBrepIntersectTol3dAttr",
             &_CreateBrepIntersectTol3dAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBrepExtentAttr",
             &This::GetBrepExtentAttr)
        .def("CreateBrepExtentAttr",
             &_CreateBrepExtentAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetBrepRegionCountAttr",
             &This::GetBrepRegionCountAttr)
        .def("CreateBrepRegionCountAttr",
             &_CreateBrepRegionCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetRegionShellCountAttr",
             &This::GetRegionShellCountAttr)
        .def("CreateRegionShellCountAttr",
             &_CreateRegionShellCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetRegionTypeAttr",
             &This::GetRegionTypeAttr)
        .def("CreateRegionTypeAttr",
             &_CreateRegionTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetShellFaceuseCountAttr",
             &This::GetShellFaceuseCountAttr)
        .def("CreateShellFaceuseCountAttr",
             &_CreateShellFaceuseCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetShellWireEdgeCountAttr",
             &This::GetShellWireEdgeCountAttr)
        .def("CreateShellWireEdgeCountAttr",
             &_CreateShellWireEdgeCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetShellPointTypeAttr",
             &This::GetShellPointTypeAttr)
        .def("CreateShellPointTypeAttr",
             &_CreateShellPointTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFaceuseFaceIndexAttr",
             &This::GetFaceuseFaceIndexAttr)
        .def("CreateFaceuseFaceIndexAttr",
             &_CreateFaceuseFaceIndexAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFaceuseOrientationTypeAttr",
             &This::GetFaceuseOrientationTypeAttr)
        .def("CreateFaceuseOrientationTypeAttr",
             &_CreateFaceuseOrientationTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFaceLoopCountAttr",
             &This::GetFaceLoopCountAttr)
        .def("CreateFaceLoopCountAttr",
             &_CreateFaceLoopCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFaceSurfaceTypeAttr",
             &This::GetFaceSurfaceTypeAttr)
        .def("CreateFaceSurfaceTypeAttr",
             &_CreateFaceSurfaceTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFaceTrimTypeAttr",
             &This::GetFaceTrimTypeAttr)
        .def("CreateFaceTrimTypeAttr",
             &_CreateFaceTrimTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFaceRangeAttr",
             &This::GetFaceRangeAttr)
        .def("CreateFaceRangeAttr",
             &_CreateFaceRangeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetLoopEdgeuseCountAttr",
             &This::GetLoopEdgeuseCountAttr)
        .def("CreateLoopEdgeuseCountAttr",
             &_CreateLoopEdgeuseCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetLoopVertexIndexAttr",
             &This::GetLoopVertexIndexAttr)
        .def("CreateLoopVertexIndexAttr",
             &_CreateLoopVertexIndexAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeuseEdgeIndexAttr",
             &This::GetEdgeuseEdgeIndexAttr)
        .def("CreateEdgeuseEdgeIndexAttr",
             &_CreateEdgeuseEdgeIndexAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeuseOrientationTypeAttr",
             &This::GetEdgeuseOrientationTypeAttr)
        .def("CreateEdgeuseOrientationTypeAttr",
             &_CreateEdgeuseOrientationTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeuseNextRadialEUIndexAttr",
             &This::GetEdgeuseNextRadialEUIndexAttr)
        .def("CreateEdgeuseNextRadialEUIndexAttr",
             &_CreateEdgeuseNextRadialEUIndexAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeuseThisRadialEntryTypeAttr",
             &This::GetEdgeuseThisRadialEntryTypeAttr)
        .def("CreateEdgeuseThisRadialEntryTypeAttr",
             &_CreateEdgeuseThisRadialEntryTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeCurveTypeAttr",
             &This::GetEdgeCurveTypeAttr)
        .def("CreateEdgeCurveTypeAttr",
             &_CreateEdgeCurveTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeRangeAttr",
             &This::GetEdgeRangeAttr)
        .def("CreateEdgeRangeAttr",
             &_CreateEdgeRangeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEdgeVertexIndicesAttr",
             &This::GetEdgeVertexIndicesAttr)
        .def("CreateEdgeVertexIndicesAttr",
             &_CreateEdgeVertexIndicesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetWireEdgeCurveTypeAttr",
             &This::GetWireEdgeCurveTypeAttr)
        .def("CreateWireEdgeCurveTypeAttr",
             &_CreateWireEdgeCurveTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetWireEdgeRangeAttr",
             &This::GetWireEdgeRangeAttr)
        .def("CreateWireEdgeRangeAttr",
             &_CreateWireEdgeRangeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetWireEdgeVertexIndicesAttr",
             &This::GetWireEdgeVertexIndicesAttr)
        .def("CreateWireEdgeVertexIndicesAttr",
             &_CreateWireEdgeVertexIndicesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetVertexPointTypeAttr",
             &This::GetVertexPointTypeAttr)
        .def("CreateVertexPointTypeAttr",
             &_CreateVertexPointTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("__repr__", ::_Repr)
    ;

    _CustomWrapCode(cls);
}

// ===================================================================== //
// Feel free to add custom code below this line, it will be preserved by 
// the code generator.  The entry point for your custom code should look
// minimally like the following:
//
// WRAP_CUSTOM {
//     _class
//         .def("MyCustomMethod", ...)
//     ;
// }
//
// Of course any other ancillary or support code may be provided.
// 
// Just remember to wrap code in the appropriate delimiters:
// 'namespace {', '}'.
//
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

WRAP_CUSTOM {
}

}

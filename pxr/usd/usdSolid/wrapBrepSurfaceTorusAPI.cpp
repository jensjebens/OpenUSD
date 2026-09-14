//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceTorusAPI.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyAnnotatedBoolResult.h"
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
_CreateSurfaceTorusOriginAttr(UsdSolidBrepSurfaceTorusAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceTorusOriginAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceTorusAxisAttr(UsdSolidBrepSurfaceTorusAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceTorusAxisAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceTorusRefDirectionAttr(UsdSolidBrepSurfaceTorusAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceTorusRefDirectionAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceTorusMajorRadiusAttr(UsdSolidBrepSurfaceTorusAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceTorusMajorRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceTorusMinorRadiusAttr(UsdSolidBrepSurfaceTorusAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceTorusMinorRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static std::string
_Repr(const UsdSolidBrepSurfaceTorusAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdSolid.BrepSurfaceTorusAPI(%s)",
        primRepr.c_str());
}

struct UsdSolidBrepSurfaceTorusAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepSurfaceTorusAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepSurfaceTorusAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdSolidBrepSurfaceTorusAPI::CanApply(prim, &whyNot);
    return UsdSolidBrepSurfaceTorusAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepSurfaceTorusAPI()
{
    typedef UsdSolidBrepSurfaceTorusAPI This;

    UsdSolidBrepSurfaceTorusAPI_CanApplyResult::Wrap<UsdSolidBrepSurfaceTorusAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepSurfaceTorusAPI");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("CanApply", &_WrapCanApply, (arg("prim")))
        .staticmethod("CanApply")

        .def("Apply", &This::Apply, (arg("prim")))
        .staticmethod("Apply")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetSurfaceTorusOriginAttr",
             &This::GetSurfaceTorusOriginAttr)
        .def("CreateSurfaceTorusOriginAttr",
             &_CreateSurfaceTorusOriginAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceTorusAxisAttr",
             &This::GetSurfaceTorusAxisAttr)
        .def("CreateSurfaceTorusAxisAttr",
             &_CreateSurfaceTorusAxisAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceTorusRefDirectionAttr",
             &This::GetSurfaceTorusRefDirectionAttr)
        .def("CreateSurfaceTorusRefDirectionAttr",
             &_CreateSurfaceTorusRefDirectionAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceTorusMajorRadiusAttr",
             &This::GetSurfaceTorusMajorRadiusAttr)
        .def("CreateSurfaceTorusMajorRadiusAttr",
             &_CreateSurfaceTorusMajorRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceTorusMinorRadiusAttr",
             &This::GetSurfaceTorusMinorRadiusAttr)
        .def("CreateSurfaceTorusMinorRadiusAttr",
             &_CreateSurfaceTorusMinorRadiusAttr,
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

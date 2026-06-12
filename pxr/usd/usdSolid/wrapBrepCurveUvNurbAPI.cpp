//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurveUvNurbAPI.h"
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
_CreateCurveUvControlVerticesAttr(UsdSolidBrepCurveUvNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurveUvControlVerticesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Double2Array), writeSparsely);
}
        
static UsdAttribute
_CreateCurveUvVertexCountAttr(UsdSolidBrepCurveUvNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurveUvVertexCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurveUvOrderAttr(UsdSolidBrepCurveUvNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurveUvOrderAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurveUvKnotsAttr(UsdSolidBrepCurveUvNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurveUvKnotsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurveUvWeightsAttr(UsdSolidBrepCurveUvNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurveUvWeightsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static std::string
_Repr(const UsdSolidBrepCurveUvNurbAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdSolid.BrepCurveUvNurbAPI(%s)",
        primRepr.c_str());
}

struct UsdSolidBrepCurveUvNurbAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepCurveUvNurbAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepCurveUvNurbAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdSolidBrepCurveUvNurbAPI::CanApply(prim, &whyNot);
    return UsdSolidBrepCurveUvNurbAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepCurveUvNurbAPI()
{
    typedef UsdSolidBrepCurveUvNurbAPI This;

    UsdSolidBrepCurveUvNurbAPI_CanApplyResult::Wrap<UsdSolidBrepCurveUvNurbAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepCurveUvNurbAPI");

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

        
        .def("GetCurveUvControlVerticesAttr",
             &This::GetCurveUvControlVerticesAttr)
        .def("CreateCurveUvControlVerticesAttr",
             &_CreateCurveUvControlVerticesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurveUvVertexCountAttr",
             &This::GetCurveUvVertexCountAttr)
        .def("CreateCurveUvVertexCountAttr",
             &_CreateCurveUvVertexCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurveUvOrderAttr",
             &This::GetCurveUvOrderAttr)
        .def("CreateCurveUvOrderAttr",
             &_CreateCurveUvOrderAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurveUvKnotsAttr",
             &This::GetCurveUvKnotsAttr)
        .def("CreateCurveUvKnotsAttr",
             &_CreateCurveUvKnotsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurveUvWeightsAttr",
             &This::GetCurveUvWeightsAttr)
        .def("CreateCurveUvWeightsAttr",
             &_CreateCurveUvWeightsAttr,
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

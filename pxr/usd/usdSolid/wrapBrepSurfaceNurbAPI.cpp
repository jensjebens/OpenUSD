//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceNurbAPI.h"
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
_CreateSurfaceControlVerticesAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceControlVerticesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceUVertexCountAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceUVertexCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceVVertexCountAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceVVertexCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceUOrderAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceUOrderAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceVOrderAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceVOrderAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceUKnotsAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceUKnotsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceVKnotsAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceVKnotsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceWeightsAttr(UsdSolidBrepSurfaceNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceWeightsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static std::string
_Repr(const UsdSolidBrepSurfaceNurbAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdSolid.BrepSurfaceNurbAPI(%s)",
        primRepr.c_str());
}

struct UsdSolidBrepSurfaceNurbAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepSurfaceNurbAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepSurfaceNurbAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdSolidBrepSurfaceNurbAPI::CanApply(prim, &whyNot);
    return UsdSolidBrepSurfaceNurbAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepSurfaceNurbAPI()
{
    typedef UsdSolidBrepSurfaceNurbAPI This;

    UsdSolidBrepSurfaceNurbAPI_CanApplyResult::Wrap<UsdSolidBrepSurfaceNurbAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepSurfaceNurbAPI");

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

        
        .def("GetSurfaceControlVerticesAttr",
             &This::GetSurfaceControlVerticesAttr)
        .def("CreateSurfaceControlVerticesAttr",
             &_CreateSurfaceControlVerticesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceUVertexCountAttr",
             &This::GetSurfaceUVertexCountAttr)
        .def("CreateSurfaceUVertexCountAttr",
             &_CreateSurfaceUVertexCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceVVertexCountAttr",
             &This::GetSurfaceVVertexCountAttr)
        .def("CreateSurfaceVVertexCountAttr",
             &_CreateSurfaceVVertexCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceUOrderAttr",
             &This::GetSurfaceUOrderAttr)
        .def("CreateSurfaceUOrderAttr",
             &_CreateSurfaceUOrderAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceVOrderAttr",
             &This::GetSurfaceVOrderAttr)
        .def("CreateSurfaceVOrderAttr",
             &_CreateSurfaceVOrderAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceUKnotsAttr",
             &This::GetSurfaceUKnotsAttr)
        .def("CreateSurfaceUKnotsAttr",
             &_CreateSurfaceUKnotsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceVKnotsAttr",
             &This::GetSurfaceVKnotsAttr)
        .def("CreateSurfaceVKnotsAttr",
             &_CreateSurfaceVKnotsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceWeightsAttr",
             &This::GetSurfaceWeightsAttr)
        .def("CreateSurfaceWeightsAttr",
             &_CreateSurfaceWeightsAttr,
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

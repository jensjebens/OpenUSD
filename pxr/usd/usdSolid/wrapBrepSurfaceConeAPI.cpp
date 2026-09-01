//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceConeAPI.h"
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
_CreateSurfaceConeOriginAttr(UsdSolidBrepSurfaceConeAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceConeOriginAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceConeAxisAttr(UsdSolidBrepSurfaceConeAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceConeAxisAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceConeRefDirectionAttr(UsdSolidBrepSurfaceConeAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceConeRefDirectionAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceConeRadiusAttr(UsdSolidBrepSurfaceConeAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceConeRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateSurfaceConeSemiAngleAttr(UsdSolidBrepSurfaceConeAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSurfaceConeSemiAngleAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static std::string
_Repr(const UsdSolidBrepSurfaceConeAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdSolid.BrepSurfaceConeAPI(%s)",
        primRepr.c_str());
}

struct UsdSolidBrepSurfaceConeAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepSurfaceConeAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepSurfaceConeAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim)
{
    std::string whyNot;
    bool result = UsdSolidBrepSurfaceConeAPI::CanApply(prim, &whyNot);
    return UsdSolidBrepSurfaceConeAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepSurfaceConeAPI()
{
    typedef UsdSolidBrepSurfaceConeAPI This;

    UsdSolidBrepSurfaceConeAPI_CanApplyResult::Wrap<UsdSolidBrepSurfaceConeAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepSurfaceConeAPI");

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

        
        .def("GetSurfaceConeOriginAttr",
             &This::GetSurfaceConeOriginAttr)
        .def("CreateSurfaceConeOriginAttr",
             &_CreateSurfaceConeOriginAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceConeAxisAttr",
             &This::GetSurfaceConeAxisAttr)
        .def("CreateSurfaceConeAxisAttr",
             &_CreateSurfaceConeAxisAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceConeRefDirectionAttr",
             &This::GetSurfaceConeRefDirectionAttr)
        .def("CreateSurfaceConeRefDirectionAttr",
             &_CreateSurfaceConeRefDirectionAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceConeRadiusAttr",
             &This::GetSurfaceConeRadiusAttr)
        .def("CreateSurfaceConeRadiusAttr",
             &_CreateSurfaceConeRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSurfaceConeSemiAngleAttr",
             &This::GetSurfaceConeSemiAngleAttr)
        .def("CreateSurfaceConeSemiAngleAttr",
             &_CreateSurfaceConeSemiAngleAttr,
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

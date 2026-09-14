//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dCircleAPI.h"
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
_CreateCurve3dCircleCenterAttr(UsdSolidBrepCurve3dCircleAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dCircleCenterAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dCircleAxisAttr(UsdSolidBrepCurve3dCircleAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dCircleAxisAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dCircleRefDirectionAttr(UsdSolidBrepCurve3dCircleAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dCircleRefDirectionAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dCircleRadiusAttr(UsdSolidBrepCurve3dCircleAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dCircleRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static bool _WrapIsBrepCurve3dCircleAPIPath(const SdfPath &path) {
    TfToken collectionName;
    return UsdSolidBrepCurve3dCircleAPI::IsBrepCurve3dCircleAPIPath(
        path, &collectionName);
}

static std::string
_Repr(const UsdSolidBrepCurve3dCircleAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    std::string instanceName = TfPyRepr(self.GetName());
    return TfStringPrintf(
        "UsdSolid.BrepCurve3dCircleAPI(%s, '%s')",
        primRepr.c_str(), instanceName.c_str());
}

struct UsdSolidBrepCurve3dCircleAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepCurve3dCircleAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepCurve3dCircleAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim, const TfToken& name)
{
    std::string whyNot;
    bool result = UsdSolidBrepCurve3dCircleAPI::CanApply(prim, name, &whyNot);
    return UsdSolidBrepCurve3dCircleAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepCurve3dCircleAPI()
{
    typedef UsdSolidBrepCurve3dCircleAPI This;

    UsdSolidBrepCurve3dCircleAPI_CanApplyResult::Wrap<UsdSolidBrepCurve3dCircleAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepCurve3dCircleAPI");

    cls
        .def(init<UsdPrim, TfToken>((arg("prim"), arg("name"))))
        .def(init<UsdSchemaBase const&, TfToken>((arg("schemaObj"), arg("name"))))
        .def(TfTypePythonClass())

        .def("Get",
            (UsdSolidBrepCurve3dCircleAPI(*)(const UsdStagePtr &stage, 
                                       const SdfPath &path))
               &This::Get,
            (arg("stage"), arg("path")))
        .def("Get",
            (UsdSolidBrepCurve3dCircleAPI(*)(const UsdPrim &prim,
                                       const TfToken &name))
               &This::Get,
            (arg("prim"), arg("name")))
        .staticmethod("Get")

        .def("GetAll",
            (std::vector<UsdSolidBrepCurve3dCircleAPI>(*)(const UsdPrim &prim))
                &This::GetAll,
            arg("prim"),
            return_value_policy<TfPySequenceToList>())
        .staticmethod("GetAll")

        .def("CanApply", &_WrapCanApply, (arg("prim"), arg("name")))
        .staticmethod("CanApply")

        .def("Apply", &This::Apply, (arg("prim"), arg("name")))
        .staticmethod("Apply")

        .def("GetSchemaAttributeNames",
             (const TfTokenVector &(*)(bool))&This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .def("GetSchemaAttributeNames",
             (TfTokenVector(*)(bool, const TfToken &))
                &This::GetSchemaAttributeNames,
             arg("includeInherited"),
             arg("instanceName"),
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetCurve3dCircleCenterAttr",
             &This::GetCurve3dCircleCenterAttr)
        .def("CreateCurve3dCircleCenterAttr",
             &_CreateCurve3dCircleCenterAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dCircleAxisAttr",
             &This::GetCurve3dCircleAxisAttr)
        .def("CreateCurve3dCircleAxisAttr",
             &_CreateCurve3dCircleAxisAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dCircleRefDirectionAttr",
             &This::GetCurve3dCircleRefDirectionAttr)
        .def("CreateCurve3dCircleRefDirectionAttr",
             &_CreateCurve3dCircleRefDirectionAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dCircleRadiusAttr",
             &This::GetCurve3dCircleRadiusAttr)
        .def("CreateCurve3dCircleRadiusAttr",
             &_CreateCurve3dCircleRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("IsBrepCurve3dCircleAPIPath", _WrapIsBrepCurve3dCircleAPIPath)
            .staticmethod("IsBrepCurve3dCircleAPIPath")
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

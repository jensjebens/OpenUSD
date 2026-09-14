//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dEllipseAPI.h"
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
_CreateCurve3dEllipseCenterAttr(UsdSolidBrepCurve3dEllipseAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dEllipseCenterAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dEllipseAxisAttr(UsdSolidBrepCurve3dEllipseAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dEllipseAxisAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dEllipseRefDirectionAttr(UsdSolidBrepCurve3dEllipseAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dEllipseRefDirectionAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dEllipseXRadiusAttr(UsdSolidBrepCurve3dEllipseAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dEllipseXRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dEllipseYRadiusAttr(UsdSolidBrepCurve3dEllipseAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dEllipseYRadiusAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static bool _WrapIsBrepCurve3dEllipseAPIPath(const SdfPath &path) {
    TfToken collectionName;
    return UsdSolidBrepCurve3dEllipseAPI::IsBrepCurve3dEllipseAPIPath(
        path, &collectionName);
}

static std::string
_Repr(const UsdSolidBrepCurve3dEllipseAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    std::string instanceName = TfPyRepr(self.GetName());
    return TfStringPrintf(
        "UsdSolid.BrepCurve3dEllipseAPI(%s, '%s')",
        primRepr.c_str(), instanceName.c_str());
}

struct UsdSolidBrepCurve3dEllipseAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepCurve3dEllipseAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepCurve3dEllipseAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim, const TfToken& name)
{
    std::string whyNot;
    bool result = UsdSolidBrepCurve3dEllipseAPI::CanApply(prim, name, &whyNot);
    return UsdSolidBrepCurve3dEllipseAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepCurve3dEllipseAPI()
{
    typedef UsdSolidBrepCurve3dEllipseAPI This;

    UsdSolidBrepCurve3dEllipseAPI_CanApplyResult::Wrap<UsdSolidBrepCurve3dEllipseAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepCurve3dEllipseAPI");

    cls
        .def(init<UsdPrim, TfToken>((arg("prim"), arg("name"))))
        .def(init<UsdSchemaBase const&, TfToken>((arg("schemaObj"), arg("name"))))
        .def(TfTypePythonClass())

        .def("Get",
            (UsdSolidBrepCurve3dEllipseAPI(*)(const UsdStagePtr &stage, 
                                       const SdfPath &path))
               &This::Get,
            (arg("stage"), arg("path")))
        .def("Get",
            (UsdSolidBrepCurve3dEllipseAPI(*)(const UsdPrim &prim,
                                       const TfToken &name))
               &This::Get,
            (arg("prim"), arg("name")))
        .staticmethod("Get")

        .def("GetAll",
            (std::vector<UsdSolidBrepCurve3dEllipseAPI>(*)(const UsdPrim &prim))
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

        
        .def("GetCurve3dEllipseCenterAttr",
             &This::GetCurve3dEllipseCenterAttr)
        .def("CreateCurve3dEllipseCenterAttr",
             &_CreateCurve3dEllipseCenterAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dEllipseAxisAttr",
             &This::GetCurve3dEllipseAxisAttr)
        .def("CreateCurve3dEllipseAxisAttr",
             &_CreateCurve3dEllipseAxisAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dEllipseRefDirectionAttr",
             &This::GetCurve3dEllipseRefDirectionAttr)
        .def("CreateCurve3dEllipseRefDirectionAttr",
             &_CreateCurve3dEllipseRefDirectionAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dEllipseXRadiusAttr",
             &This::GetCurve3dEllipseXRadiusAttr)
        .def("CreateCurve3dEllipseXRadiusAttr",
             &_CreateCurve3dEllipseXRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dEllipseYRadiusAttr",
             &This::GetCurve3dEllipseYRadiusAttr)
        .def("CreateCurve3dEllipseYRadiusAttr",
             &_CreateCurve3dEllipseYRadiusAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("IsBrepCurve3dEllipseAPIPath", _WrapIsBrepCurve3dEllipseAPIPath)
            .staticmethod("IsBrepCurve3dEllipseAPIPath")
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

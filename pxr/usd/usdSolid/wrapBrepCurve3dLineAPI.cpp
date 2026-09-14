//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dLineAPI.h"
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
_CreateCurve3dLineOriginAttr(UsdSolidBrepCurve3dLineAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dLineOriginAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dLineDirectionAttr(UsdSolidBrepCurve3dLineAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dLineDirectionAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Vector3dArray), writeSparsely);
}

static bool _WrapIsBrepCurve3dLineAPIPath(const SdfPath &path) {
    TfToken collectionName;
    return UsdSolidBrepCurve3dLineAPI::IsBrepCurve3dLineAPIPath(
        path, &collectionName);
}

static std::string
_Repr(const UsdSolidBrepCurve3dLineAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    std::string instanceName = TfPyRepr(self.GetName());
    return TfStringPrintf(
        "UsdSolid.BrepCurve3dLineAPI(%s, '%s')",
        primRepr.c_str(), instanceName.c_str());
}

struct UsdSolidBrepCurve3dLineAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepCurve3dLineAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepCurve3dLineAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim, const TfToken& name)
{
    std::string whyNot;
    bool result = UsdSolidBrepCurve3dLineAPI::CanApply(prim, name, &whyNot);
    return UsdSolidBrepCurve3dLineAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepCurve3dLineAPI()
{
    typedef UsdSolidBrepCurve3dLineAPI This;

    UsdSolidBrepCurve3dLineAPI_CanApplyResult::Wrap<UsdSolidBrepCurve3dLineAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepCurve3dLineAPI");

    cls
        .def(init<UsdPrim, TfToken>((arg("prim"), arg("name"))))
        .def(init<UsdSchemaBase const&, TfToken>((arg("schemaObj"), arg("name"))))
        .def(TfTypePythonClass())

        .def("Get",
            (UsdSolidBrepCurve3dLineAPI(*)(const UsdStagePtr &stage, 
                                       const SdfPath &path))
               &This::Get,
            (arg("stage"), arg("path")))
        .def("Get",
            (UsdSolidBrepCurve3dLineAPI(*)(const UsdPrim &prim,
                                       const TfToken &name))
               &This::Get,
            (arg("prim"), arg("name")))
        .staticmethod("Get")

        .def("GetAll",
            (std::vector<UsdSolidBrepCurve3dLineAPI>(*)(const UsdPrim &prim))
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

        
        .def("GetCurve3dLineOriginAttr",
             &This::GetCurve3dLineOriginAttr)
        .def("CreateCurve3dLineOriginAttr",
             &_CreateCurve3dLineOriginAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dLineDirectionAttr",
             &This::GetCurve3dLineDirectionAttr)
        .def("CreateCurve3dLineDirectionAttr",
             &_CreateCurve3dLineDirectionAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("IsBrepCurve3dLineAPIPath", _WrapIsBrepCurve3dLineAPIPath)
            .staticmethod("IsBrepCurve3dLineAPIPath")
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

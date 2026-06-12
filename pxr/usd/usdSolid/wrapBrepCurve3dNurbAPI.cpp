//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dNurbAPI.h"
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
_CreateCurve3dControlVerticesAttr(UsdSolidBrepCurve3dNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dControlVerticesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Point3dArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dVertexCountAttr(UsdSolidBrepCurve3dNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dVertexCountAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dOrderAttr(UsdSolidBrepCurve3dNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dOrderAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->UIntArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dKnotsAttr(UsdSolidBrepCurve3dNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dKnotsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}
        
static UsdAttribute
_CreateCurve3dWeightsAttr(UsdSolidBrepCurve3dNurbAPI &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCurve3dWeightsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->DoubleArray), writeSparsely);
}

static bool _WrapIsBrepCurve3dNurbAPIPath(const SdfPath &path) {
    TfToken collectionName;
    return UsdSolidBrepCurve3dNurbAPI::IsBrepCurve3dNurbAPIPath(
        path, &collectionName);
}

static std::string
_Repr(const UsdSolidBrepCurve3dNurbAPI &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    std::string instanceName = TfPyRepr(self.GetName());
    return TfStringPrintf(
        "UsdSolid.BrepCurve3dNurbAPI(%s, '%s')",
        primRepr.c_str(), instanceName.c_str());
}

struct UsdSolidBrepCurve3dNurbAPI_CanApplyResult : 
    public TfPyAnnotatedBoolResult<std::string>
{
    UsdSolidBrepCurve3dNurbAPI_CanApplyResult(bool val, std::string const &msg) :
        TfPyAnnotatedBoolResult<std::string>(val, msg) {}
};

static UsdSolidBrepCurve3dNurbAPI_CanApplyResult
_WrapCanApply(const UsdPrim& prim, const TfToken& name)
{
    std::string whyNot;
    bool result = UsdSolidBrepCurve3dNurbAPI::CanApply(prim, name, &whyNot);
    return UsdSolidBrepCurve3dNurbAPI_CanApplyResult(result, whyNot);
}

} // anonymous namespace

void wrapUsdSolidBrepCurve3dNurbAPI()
{
    typedef UsdSolidBrepCurve3dNurbAPI This;

    UsdSolidBrepCurve3dNurbAPI_CanApplyResult::Wrap<UsdSolidBrepCurve3dNurbAPI_CanApplyResult>(
        "_CanApplyResult", "whyNot");

    class_<This, bases<UsdAPISchemaBase> >
        cls("BrepCurve3dNurbAPI");

    cls
        .def(init<UsdPrim, TfToken>((arg("prim"), arg("name"))))
        .def(init<UsdSchemaBase const&, TfToken>((arg("schemaObj"), arg("name"))))
        .def(TfTypePythonClass())

        .def("Get",
            (UsdSolidBrepCurve3dNurbAPI(*)(const UsdStagePtr &stage, 
                                       const SdfPath &path))
               &This::Get,
            (arg("stage"), arg("path")))
        .def("Get",
            (UsdSolidBrepCurve3dNurbAPI(*)(const UsdPrim &prim,
                                       const TfToken &name))
               &This::Get,
            (arg("prim"), arg("name")))
        .staticmethod("Get")

        .def("GetAll",
            (std::vector<UsdSolidBrepCurve3dNurbAPI>(*)(const UsdPrim &prim))
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

        
        .def("GetCurve3dControlVerticesAttr",
             &This::GetCurve3dControlVerticesAttr)
        .def("CreateCurve3dControlVerticesAttr",
             &_CreateCurve3dControlVerticesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dVertexCountAttr",
             &This::GetCurve3dVertexCountAttr)
        .def("CreateCurve3dVertexCountAttr",
             &_CreateCurve3dVertexCountAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dOrderAttr",
             &This::GetCurve3dOrderAttr)
        .def("CreateCurve3dOrderAttr",
             &_CreateCurve3dOrderAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dKnotsAttr",
             &This::GetCurve3dKnotsAttr)
        .def("CreateCurve3dKnotsAttr",
             &_CreateCurve3dKnotsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCurve3dWeightsAttr",
             &This::GetCurve3dWeightsAttr)
        .def("CreateCurve3dWeightsAttr",
             &_CreateCurve3dWeightsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("IsBrepCurve3dNurbAPIPath", _WrapIsBrepCurve3dNurbAPIPath)
            .staticmethod("IsBrepCurve3dNurbAPIPath")
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

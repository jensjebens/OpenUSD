//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./geomMetricsAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdMetricsGeomMetricsAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdMetricsGeomMetricsAPI::~UsdMetricsGeomMetricsAPI()
{
}

/* static */
UsdMetricsGeomMetricsAPI
UsdMetricsGeomMetricsAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdMetricsGeomMetricsAPI();
    }
    return UsdMetricsGeomMetricsAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdMetricsGeomMetricsAPI::_GetSchemaKind() const
{
    return UsdMetricsGeomMetricsAPI::schemaKind;
}

/* static */
bool
UsdMetricsGeomMetricsAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdMetricsGeomMetricsAPI>(whyNot);
}

/* static */
UsdMetricsGeomMetricsAPI
UsdMetricsGeomMetricsAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdMetricsGeomMetricsAPI>()) {
        return UsdMetricsGeomMetricsAPI(prim);
    }
    return UsdMetricsGeomMetricsAPI();
}

/* static */
const TfType &
UsdMetricsGeomMetricsAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdMetricsGeomMetricsAPI>();
    return tfType;
}

/* static */
bool 
UsdMetricsGeomMetricsAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdMetricsGeomMetricsAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdMetricsGeomMetricsAPI::GetMetersPerUnitAttr() const
{
    return GetPrim().GetAttribute(UsdMetricsTokens->metricsMetersPerUnit);
}

UsdAttribute
UsdMetricsGeomMetricsAPI::CreateMetersPerUnitAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdMetricsTokens->metricsMetersPerUnit,
                       SdfValueTypeNames->Double,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdMetricsGeomMetricsAPI::GetUpAxisAttr() const
{
    return GetPrim().GetAttribute(UsdMetricsTokens->metricsUpAxis);
}

UsdAttribute
UsdMetricsGeomMetricsAPI::CreateUpAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdMetricsTokens->metricsUpAxis,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left,const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

/*static*/
const TfTokenVector&
UsdMetricsGeomMetricsAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdMetricsTokens->metricsMetersPerUnit,
        UsdMetricsTokens->metricsUpAxis,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdAPISchemaBase::GetSchemaAttributeNames(true),
            localNames);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

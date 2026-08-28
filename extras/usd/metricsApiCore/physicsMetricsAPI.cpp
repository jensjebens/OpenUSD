//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./physicsMetricsAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdMetricsPhysicsMetricsAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdMetricsPhysicsMetricsAPI::~UsdMetricsPhysicsMetricsAPI()
{
}

/* static */
UsdMetricsPhysicsMetricsAPI
UsdMetricsPhysicsMetricsAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdMetricsPhysicsMetricsAPI();
    }
    return UsdMetricsPhysicsMetricsAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdMetricsPhysicsMetricsAPI::_GetSchemaKind() const
{
    return UsdMetricsPhysicsMetricsAPI::schemaKind;
}

/* static */
bool
UsdMetricsPhysicsMetricsAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdMetricsPhysicsMetricsAPI>(whyNot);
}

/* static */
UsdMetricsPhysicsMetricsAPI
UsdMetricsPhysicsMetricsAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdMetricsPhysicsMetricsAPI>()) {
        return UsdMetricsPhysicsMetricsAPI(prim);
    }
    return UsdMetricsPhysicsMetricsAPI();
}

/* static */
const TfType &
UsdMetricsPhysicsMetricsAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdMetricsPhysicsMetricsAPI>();
    return tfType;
}

/* static */
bool 
UsdMetricsPhysicsMetricsAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdMetricsPhysicsMetricsAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdMetricsPhysicsMetricsAPI::GetKilogramsPerUnitAttr() const
{
    return GetPrim().GetAttribute(UsdMetricsTokens->metricsKilogramsPerUnit);
}

UsdAttribute
UsdMetricsPhysicsMetricsAPI::CreateKilogramsPerUnitAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdMetricsTokens->metricsKilogramsPerUnit,
                       SdfValueTypeNames->Double,
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
UsdMetricsPhysicsMetricsAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdMetricsTokens->metricsKilogramsPerUnit,
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

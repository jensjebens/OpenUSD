//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceTorusAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepSurfaceTorusAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepSurfaceTorusAPI::~UsdSolidBrepSurfaceTorusAPI()
{
}

/* static */
UsdSolidBrepSurfaceTorusAPI
UsdSolidBrepSurfaceTorusAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepSurfaceTorusAPI();
    }
    return UsdSolidBrepSurfaceTorusAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepSurfaceTorusAPI::_GetSchemaKind() const
{
    return UsdSolidBrepSurfaceTorusAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepSurfaceTorusAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepSurfaceTorusAPI>(whyNot);
}

/* static */
UsdSolidBrepSurfaceTorusAPI
UsdSolidBrepSurfaceTorusAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepSurfaceTorusAPI>()) {
        return UsdSolidBrepSurfaceTorusAPI(prim);
    }
    return UsdSolidBrepSurfaceTorusAPI();
}

/* static */
const TfType &
UsdSolidBrepSurfaceTorusAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepSurfaceTorusAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepSurfaceTorusAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepSurfaceTorusAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::GetSurfaceTorusOriginAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceTorusOrigin);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::CreateSurfaceTorusOriginAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceTorusOrigin,
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::GetSurfaceTorusAxisAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceTorusAxis);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::CreateSurfaceTorusAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceTorusAxis,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::GetSurfaceTorusRefDirectionAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceTorusRefDirection);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::CreateSurfaceTorusRefDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceTorusRefDirection,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::GetSurfaceTorusMajorRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceTorusMajorRadius);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::CreateSurfaceTorusMajorRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceTorusMajorRadius,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::GetSurfaceTorusMinorRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceTorusMinorRadius);
}

UsdAttribute
UsdSolidBrepSurfaceTorusAPI::CreateSurfaceTorusMinorRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceTorusMinorRadius,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
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
UsdSolidBrepSurfaceTorusAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepSurfaceTorusOrigin,
        UsdSolidTokens->brepSurfaceTorusAxis,
        UsdSolidTokens->brepSurfaceTorusRefDirection,
        UsdSolidTokens->brepSurfaceTorusMajorRadius,
        UsdSolidTokens->brepSurfaceTorusMinorRadius,
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

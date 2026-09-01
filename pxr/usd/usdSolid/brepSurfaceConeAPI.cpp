//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceConeAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepSurfaceConeAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepSurfaceConeAPI::~UsdSolidBrepSurfaceConeAPI()
{
}

/* static */
UsdSolidBrepSurfaceConeAPI
UsdSolidBrepSurfaceConeAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepSurfaceConeAPI();
    }
    return UsdSolidBrepSurfaceConeAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepSurfaceConeAPI::_GetSchemaKind() const
{
    return UsdSolidBrepSurfaceConeAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepSurfaceConeAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepSurfaceConeAPI>(whyNot);
}

/* static */
UsdSolidBrepSurfaceConeAPI
UsdSolidBrepSurfaceConeAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepSurfaceConeAPI>()) {
        return UsdSolidBrepSurfaceConeAPI(prim);
    }
    return UsdSolidBrepSurfaceConeAPI();
}

/* static */
const TfType &
UsdSolidBrepSurfaceConeAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepSurfaceConeAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepSurfaceConeAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepSurfaceConeAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::GetSurfaceConeOriginAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceConeOrigin);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::CreateSurfaceConeOriginAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceConeOrigin,
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::GetSurfaceConeAxisAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceConeAxis);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::CreateSurfaceConeAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceConeAxis,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::GetSurfaceConeRefDirectionAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceConeRefDirection);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::CreateSurfaceConeRefDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceConeRefDirection,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::GetSurfaceConeRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceConeRadius);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::CreateSurfaceConeRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceConeRadius,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::GetSurfaceConeSemiAngleAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceConeSemiAngle);
}

UsdAttribute
UsdSolidBrepSurfaceConeAPI::CreateSurfaceConeSemiAngleAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceConeSemiAngle,
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
UsdSolidBrepSurfaceConeAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepSurfaceConeOrigin,
        UsdSolidTokens->brepSurfaceConeAxis,
        UsdSolidTokens->brepSurfaceConeRefDirection,
        UsdSolidTokens->brepSurfaceConeRadius,
        UsdSolidTokens->brepSurfaceConeSemiAngle,
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

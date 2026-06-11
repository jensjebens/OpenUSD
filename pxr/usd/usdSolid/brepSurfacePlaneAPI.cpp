//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfacePlaneAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepSurfacePlaneAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepSurfacePlaneAPI::~UsdSolidBrepSurfacePlaneAPI()
{
}

/* static */
UsdSolidBrepSurfacePlaneAPI
UsdSolidBrepSurfacePlaneAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepSurfacePlaneAPI();
    }
    return UsdSolidBrepSurfacePlaneAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepSurfacePlaneAPI::_GetSchemaKind() const
{
    return UsdSolidBrepSurfacePlaneAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepSurfacePlaneAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepSurfacePlaneAPI>(whyNot);
}

/* static */
UsdSolidBrepSurfacePlaneAPI
UsdSolidBrepSurfacePlaneAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepSurfacePlaneAPI>()) {
        return UsdSolidBrepSurfacePlaneAPI(prim);
    }
    return UsdSolidBrepSurfacePlaneAPI();
}

/* static */
const TfType &
UsdSolidBrepSurfacePlaneAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepSurfacePlaneAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepSurfacePlaneAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepSurfacePlaneAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepSurfacePlaneAPI::GetSurfacePlaneOriginAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfacePlaneOrigin);
}

UsdAttribute
UsdSolidBrepSurfacePlaneAPI::CreateSurfacePlaneOriginAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfacePlaneOrigin,
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfacePlaneAPI::GetSurfacePlaneAxisAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfacePlaneAxis);
}

UsdAttribute
UsdSolidBrepSurfacePlaneAPI::CreateSurfacePlaneAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfacePlaneAxis,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfacePlaneAPI::GetSurfacePlaneRefDirectionAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfacePlaneRefDirection);
}

UsdAttribute
UsdSolidBrepSurfacePlaneAPI::CreateSurfacePlaneRefDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfacePlaneRefDirection,
                       SdfValueTypeNames->Vector3dArray,
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
UsdSolidBrepSurfacePlaneAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepSurfacePlaneOrigin,
        UsdSolidTokens->brepSurfacePlaneAxis,
        UsdSolidTokens->brepSurfacePlaneRefDirection,
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

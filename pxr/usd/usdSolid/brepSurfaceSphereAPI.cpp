//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceSphereAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepSurfaceSphereAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepSurfaceSphereAPI::~UsdSolidBrepSurfaceSphereAPI()
{
}

/* static */
UsdSolidBrepSurfaceSphereAPI
UsdSolidBrepSurfaceSphereAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepSurfaceSphereAPI();
    }
    return UsdSolidBrepSurfaceSphereAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepSurfaceSphereAPI::_GetSchemaKind() const
{
    return UsdSolidBrepSurfaceSphereAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepSurfaceSphereAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepSurfaceSphereAPI>(whyNot);
}

/* static */
UsdSolidBrepSurfaceSphereAPI
UsdSolidBrepSurfaceSphereAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepSurfaceSphereAPI>()) {
        return UsdSolidBrepSurfaceSphereAPI(prim);
    }
    return UsdSolidBrepSurfaceSphereAPI();
}

/* static */
const TfType &
UsdSolidBrepSurfaceSphereAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepSurfaceSphereAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepSurfaceSphereAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepSurfaceSphereAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::GetSurfaceSphereCenterAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceSphereCenter);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::CreateSurfaceSphereCenterAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceSphereCenter,
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::GetSurfaceSphereAxisAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceSphereAxis);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::CreateSurfaceSphereAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceSphereAxis,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::GetSurfaceSphereRefDirectionAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceSphereRefDirection);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::CreateSurfaceSphereRefDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceSphereRefDirection,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::GetSurfaceSphereRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceSphereRadius);
}

UsdAttribute
UsdSolidBrepSurfaceSphereAPI::CreateSurfaceSphereRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceSphereRadius,
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
UsdSolidBrepSurfaceSphereAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepSurfaceSphereCenter,
        UsdSolidTokens->brepSurfaceSphereAxis,
        UsdSolidTokens->brepSurfaceSphereRefDirection,
        UsdSolidTokens->brepSurfaceSphereRadius,
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

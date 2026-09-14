//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceCylinderAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepSurfaceCylinderAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepSurfaceCylinderAPI::~UsdSolidBrepSurfaceCylinderAPI()
{
}

/* static */
UsdSolidBrepSurfaceCylinderAPI
UsdSolidBrepSurfaceCylinderAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepSurfaceCylinderAPI();
    }
    return UsdSolidBrepSurfaceCylinderAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepSurfaceCylinderAPI::_GetSchemaKind() const
{
    return UsdSolidBrepSurfaceCylinderAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepSurfaceCylinderAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepSurfaceCylinderAPI>(whyNot);
}

/* static */
UsdSolidBrepSurfaceCylinderAPI
UsdSolidBrepSurfaceCylinderAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepSurfaceCylinderAPI>()) {
        return UsdSolidBrepSurfaceCylinderAPI(prim);
    }
    return UsdSolidBrepSurfaceCylinderAPI();
}

/* static */
const TfType &
UsdSolidBrepSurfaceCylinderAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepSurfaceCylinderAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepSurfaceCylinderAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepSurfaceCylinderAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::GetSurfaceCylinderOriginAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceCylinderOrigin);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::CreateSurfaceCylinderOriginAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceCylinderOrigin,
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::GetSurfaceCylinderAxisAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceCylinderAxis);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::CreateSurfaceCylinderAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceCylinderAxis,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::GetSurfaceCylinderRefDirectionAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceCylinderRefDirection);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::CreateSurfaceCylinderRefDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceCylinderRefDirection,
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::GetSurfaceCylinderRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceCylinderRadius);
}

UsdAttribute
UsdSolidBrepSurfaceCylinderAPI::CreateSurfaceCylinderRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceCylinderRadius,
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
UsdSolidBrepSurfaceCylinderAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepSurfaceCylinderOrigin,
        UsdSolidTokens->brepSurfaceCylinderAxis,
        UsdSolidTokens->brepSurfaceCylinderRefDirection,
        UsdSolidTokens->brepSurfaceCylinderRadius,
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

//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/profileAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdProfilesProfileAPI,
        TfType::Bases< UsdAPISchemaBase > >();
}

/* virtual */
UsdProfilesProfileAPI::~UsdProfilesProfileAPI()
{
}

/* static */
UsdProfilesProfileAPI
UsdProfilesProfileAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdProfilesProfileAPI();
    }
    return UsdProfilesProfileAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdProfilesProfileAPI::_GetSchemaKind() const
{
    return UsdProfilesProfileAPI::schemaKind;
}

/* static */
bool
UsdProfilesProfileAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdProfilesProfileAPI>(whyNot);
}

/* static */
UsdProfilesProfileAPI
UsdProfilesProfileAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdProfilesProfileAPI>()) {
        return UsdProfilesProfileAPI(prim);
    }
    return UsdProfilesProfileAPI();
}

/* static */
const TfType &
UsdProfilesProfileAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdProfilesProfileAPI>();
    return tfType;
}

/* static */
bool
UsdProfilesProfileAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdProfilesProfileAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdProfilesProfileAPI::GetProfileAttr() const
{
    return GetPrim().GetAttribute(UsdProfilesTokens->profilesProfile);
}

UsdAttribute
UsdProfilesProfileAPI::CreateProfileAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdProfilesTokens->profilesProfile,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdProfilesProfileAPI::GetCapabilitiesAttr() const
{
    return GetPrim().GetAttribute(UsdProfilesTokens->profilesCapabilities);
}

UsdAttribute
UsdProfilesProfileAPI::CreateCapabilitiesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdProfilesTokens->profilesCapabilities,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left, const TfTokenVector& right)
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
UsdProfilesProfileAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdProfilesTokens->profilesProfile,
        UsdProfilesTokens->profilesCapabilities,
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

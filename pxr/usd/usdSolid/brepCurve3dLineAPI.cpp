//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dLineAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepCurve3dLineAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepCurve3dLineAPI::~UsdSolidBrepCurve3dLineAPI()
{
}

/* static */
UsdSolidBrepCurve3dLineAPI
UsdSolidBrepCurve3dLineAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepCurve3dLineAPI();
    }
    TfToken name;
    if (!IsBrepCurve3dLineAPIPath(path, &name)) {
        TF_CODING_ERROR("Invalid brep path <%s>.", path.GetText());
        return UsdSolidBrepCurve3dLineAPI();
    }
    return UsdSolidBrepCurve3dLineAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdSolidBrepCurve3dLineAPI
UsdSolidBrepCurve3dLineAPI::Get(const UsdPrim &prim, const TfToken &name)
{
    return UsdSolidBrepCurve3dLineAPI(prim, name);
}

/* static */
std::vector<UsdSolidBrepCurve3dLineAPI>
UsdSolidBrepCurve3dLineAPI::GetAll(const UsdPrim &prim)
{
    std::vector<UsdSolidBrepCurve3dLineAPI> schemas;
    
    for (const auto &schemaName :
         UsdAPISchemaBase::_GetMultipleApplyInstanceNames(prim, _GetStaticTfType())) {
        schemas.emplace_back(prim, schemaName);
    }

    return schemas;
}


/* static */
bool 
UsdSolidBrepCurve3dLineAPI::IsSchemaPropertyBaseName(const TfToken &baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineOrigin),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineDirection),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName)
            != attrsAndRels.end();
}

/* static */
bool
UsdSolidBrepCurve3dLineAPI::IsBrepCurve3dLineAPIPath(
    const SdfPath &path, TfToken *name)
{
    if (!path.IsPropertyPath()) {
        return false;
    }

    std::string propertyName = path.GetName();
    TfTokenVector tokens = SdfPath::TokenizeIdentifierAsTokens(propertyName);

    // The baseName of the  path can't be one of the 
    // schema properties. We should validate this in the creation (or apply)
    // API.
    TfToken baseName = *tokens.rbegin();
    if (IsSchemaPropertyBaseName(baseName)) {
        return false;
    }

    if (tokens.size() >= 2
        && tokens[0] == UsdSolidTokens->brep) {
        *name = TfToken(propertyName.substr(
           UsdSolidTokens->brep.GetString().size() + 1));
        return true;
    }

    return false;
}

/* virtual */
UsdSchemaKind UsdSolidBrepCurve3dLineAPI::_GetSchemaKind() const
{
    return UsdSolidBrepCurve3dLineAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepCurve3dLineAPI::CanApply(
    const UsdPrim &prim, const TfToken &name, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepCurve3dLineAPI>(name, whyNot);
}

/* static */
UsdSolidBrepCurve3dLineAPI
UsdSolidBrepCurve3dLineAPI::Apply(const UsdPrim &prim, const TfToken &name)
{
    if (prim.ApplyAPI<UsdSolidBrepCurve3dLineAPI>(name)) {
        return UsdSolidBrepCurve3dLineAPI(prim, name);
    }
    return UsdSolidBrepCurve3dLineAPI();
}

/* static */
const TfType &
UsdSolidBrepCurve3dLineAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepCurve3dLineAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepCurve3dLineAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepCurve3dLineAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

/// Returns the property name prefixed with the correct namespace prefix, which
/// is composed of the the API's propertyNamespacePrefix metadata and the
/// instance name of the API.
static inline
TfToken
_GetNamespacedPropertyName(const TfToken instanceName, const TfToken propName)
{
    return UsdSchemaRegistry::MakeMultipleApplyNameInstance(propName, instanceName);
}

UsdAttribute
UsdSolidBrepCurve3dLineAPI::GetCurve3dLineOriginAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineOrigin));
}

UsdAttribute
UsdSolidBrepCurve3dLineAPI::CreateCurve3dLineOriginAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineOrigin),
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dLineAPI::GetCurve3dLineDirectionAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineDirection));
}

UsdAttribute
UsdSolidBrepCurve3dLineAPI::CreateCurve3dLineDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineDirection),
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
UsdSolidBrepCurve3dLineAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineOrigin,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dLineDirection,
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

/*static*/
TfTokenVector
UsdSolidBrepCurve3dLineAPI::GetSchemaAttributeNames(
    bool includeInherited, const TfToken &instanceName)
{
    const TfTokenVector &attrNames = GetSchemaAttributeNames(includeInherited);
    if (instanceName.IsEmpty()) {
        return attrNames;
    }
    TfTokenVector result;
    result.reserve(attrNames.size());
    for (const TfToken &attrName : attrNames) {
        result.push_back(
            UsdSchemaRegistry::MakeMultipleApplyNameInstance(attrName, instanceName));
    }
    return result;
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

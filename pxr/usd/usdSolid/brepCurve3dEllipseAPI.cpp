//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dEllipseAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepCurve3dEllipseAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepCurve3dEllipseAPI::~UsdSolidBrepCurve3dEllipseAPI()
{
}

/* static */
UsdSolidBrepCurve3dEllipseAPI
UsdSolidBrepCurve3dEllipseAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepCurve3dEllipseAPI();
    }
    TfToken name;
    if (!IsBrepCurve3dEllipseAPIPath(path, &name)) {
        TF_CODING_ERROR("Invalid brep path <%s>.", path.GetText());
        return UsdSolidBrepCurve3dEllipseAPI();
    }
    return UsdSolidBrepCurve3dEllipseAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdSolidBrepCurve3dEllipseAPI
UsdSolidBrepCurve3dEllipseAPI::Get(const UsdPrim &prim, const TfToken &name)
{
    return UsdSolidBrepCurve3dEllipseAPI(prim, name);
}

/* static */
std::vector<UsdSolidBrepCurve3dEllipseAPI>
UsdSolidBrepCurve3dEllipseAPI::GetAll(const UsdPrim &prim)
{
    std::vector<UsdSolidBrepCurve3dEllipseAPI> schemas;
    
    for (const auto &schemaName :
         UsdAPISchemaBase::_GetMultipleApplyInstanceNames(prim, _GetStaticTfType())) {
        schemas.emplace_back(prim, schemaName);
    }

    return schemas;
}


/* static */
bool 
UsdSolidBrepCurve3dEllipseAPI::IsSchemaPropertyBaseName(const TfToken &baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseCenter),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseAxis),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseRefDirection),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseXRadius),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseYRadius),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName)
            != attrsAndRels.end();
}

/* static */
bool
UsdSolidBrepCurve3dEllipseAPI::IsBrepCurve3dEllipseAPIPath(
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
UsdSchemaKind UsdSolidBrepCurve3dEllipseAPI::_GetSchemaKind() const
{
    return UsdSolidBrepCurve3dEllipseAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepCurve3dEllipseAPI::CanApply(
    const UsdPrim &prim, const TfToken &name, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepCurve3dEllipseAPI>(name, whyNot);
}

/* static */
UsdSolidBrepCurve3dEllipseAPI
UsdSolidBrepCurve3dEllipseAPI::Apply(const UsdPrim &prim, const TfToken &name)
{
    if (prim.ApplyAPI<UsdSolidBrepCurve3dEllipseAPI>(name)) {
        return UsdSolidBrepCurve3dEllipseAPI(prim, name);
    }
    return UsdSolidBrepCurve3dEllipseAPI();
}

/* static */
const TfType &
UsdSolidBrepCurve3dEllipseAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepCurve3dEllipseAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepCurve3dEllipseAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepCurve3dEllipseAPI::_GetTfType() const
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
UsdSolidBrepCurve3dEllipseAPI::GetCurve3dEllipseCenterAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseCenter));
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::CreateCurve3dEllipseCenterAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseCenter),
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::GetCurve3dEllipseAxisAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseAxis));
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::CreateCurve3dEllipseAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseAxis),
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::GetCurve3dEllipseRefDirectionAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseRefDirection));
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::CreateCurve3dEllipseRefDirectionAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseRefDirection),
                       SdfValueTypeNames->Vector3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::GetCurve3dEllipseXRadiusAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseXRadius));
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::CreateCurve3dEllipseXRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseXRadius),
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::GetCurve3dEllipseYRadiusAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseYRadius));
}

UsdAttribute
UsdSolidBrepCurve3dEllipseAPI::CreateCurve3dEllipseYRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseYRadius),
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
UsdSolidBrepCurve3dEllipseAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseCenter,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseAxis,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseRefDirection,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseXRadius,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dEllipseYRadius,
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
UsdSolidBrepCurve3dEllipseAPI::GetSchemaAttributeNames(
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

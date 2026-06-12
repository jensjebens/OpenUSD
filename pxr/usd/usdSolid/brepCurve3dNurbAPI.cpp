//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurve3dNurbAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepCurve3dNurbAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepCurve3dNurbAPI::~UsdSolidBrepCurve3dNurbAPI()
{
}

/* static */
UsdSolidBrepCurve3dNurbAPI
UsdSolidBrepCurve3dNurbAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepCurve3dNurbAPI();
    }
    TfToken name;
    if (!IsBrepCurve3dNurbAPIPath(path, &name)) {
        TF_CODING_ERROR("Invalid brep path <%s>.", path.GetText());
        return UsdSolidBrepCurve3dNurbAPI();
    }
    return UsdSolidBrepCurve3dNurbAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdSolidBrepCurve3dNurbAPI
UsdSolidBrepCurve3dNurbAPI::Get(const UsdPrim &prim, const TfToken &name)
{
    return UsdSolidBrepCurve3dNurbAPI(prim, name);
}

/* static */
std::vector<UsdSolidBrepCurve3dNurbAPI>
UsdSolidBrepCurve3dNurbAPI::GetAll(const UsdPrim &prim)
{
    std::vector<UsdSolidBrepCurve3dNurbAPI> schemas;
    
    for (const auto &schemaName :
         UsdAPISchemaBase::_GetMultipleApplyInstanceNames(prim, _GetStaticTfType())) {
        schemas.emplace_back(prim, schemaName);
    }

    return schemas;
}


/* static */
bool 
UsdSolidBrepCurve3dNurbAPI::IsSchemaPropertyBaseName(const TfToken &baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbControlVertices),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbVertexCount),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbOrder),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbKnots),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbWeights),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName)
            != attrsAndRels.end();
}

/* static */
bool
UsdSolidBrepCurve3dNurbAPI::IsBrepCurve3dNurbAPIPath(
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
UsdSchemaKind UsdSolidBrepCurve3dNurbAPI::_GetSchemaKind() const
{
    return UsdSolidBrepCurve3dNurbAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepCurve3dNurbAPI::CanApply(
    const UsdPrim &prim, const TfToken &name, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepCurve3dNurbAPI>(name, whyNot);
}

/* static */
UsdSolidBrepCurve3dNurbAPI
UsdSolidBrepCurve3dNurbAPI::Apply(const UsdPrim &prim, const TfToken &name)
{
    if (prim.ApplyAPI<UsdSolidBrepCurve3dNurbAPI>(name)) {
        return UsdSolidBrepCurve3dNurbAPI(prim, name);
    }
    return UsdSolidBrepCurve3dNurbAPI();
}

/* static */
const TfType &
UsdSolidBrepCurve3dNurbAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepCurve3dNurbAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepCurve3dNurbAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepCurve3dNurbAPI::_GetTfType() const
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
UsdSolidBrepCurve3dNurbAPI::GetCurve3dControlVerticesAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbControlVertices));
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::CreateCurve3dControlVerticesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbControlVertices),
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::GetCurve3dVertexCountAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbVertexCount));
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::CreateCurve3dVertexCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbVertexCount),
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::GetCurve3dOrderAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbOrder));
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::CreateCurve3dOrderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbOrder),
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::GetCurve3dKnotsAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbKnots));
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::CreateCurve3dKnotsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbKnots),
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::GetCurve3dWeightsAttr() const
{
    return GetPrim().GetAttribute(
        _GetNamespacedPropertyName(
            GetName(),
            UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbWeights));
}

UsdAttribute
UsdSolidBrepCurve3dNurbAPI::CreateCurve3dWeightsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
                       _GetNamespacedPropertyName(
                            GetName(),
                           UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbWeights),
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
UsdSolidBrepCurve3dNurbAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbControlVertices,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbVertexCount,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbOrder,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbKnots,
        UsdSolidTokens->brep_MultipleApplyTemplate_Curve3dNurbWeights,
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
UsdSolidBrepCurve3dNurbAPI::GetSchemaAttributeNames(
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

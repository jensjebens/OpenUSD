//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepCurveUvNurbAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepCurveUvNurbAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepCurveUvNurbAPI::~UsdSolidBrepCurveUvNurbAPI()
{
}

/* static */
UsdSolidBrepCurveUvNurbAPI
UsdSolidBrepCurveUvNurbAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepCurveUvNurbAPI();
    }
    return UsdSolidBrepCurveUvNurbAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepCurveUvNurbAPI::_GetSchemaKind() const
{
    return UsdSolidBrepCurveUvNurbAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepCurveUvNurbAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepCurveUvNurbAPI>(whyNot);
}

/* static */
UsdSolidBrepCurveUvNurbAPI
UsdSolidBrepCurveUvNurbAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepCurveUvNurbAPI>()) {
        return UsdSolidBrepCurveUvNurbAPI(prim);
    }
    return UsdSolidBrepCurveUvNurbAPI();
}

/* static */
const TfType &
UsdSolidBrepCurveUvNurbAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepCurveUvNurbAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepCurveUvNurbAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepCurveUvNurbAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::GetCurveUvControlVerticesAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepCurveUvNurbControlVertices);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::CreateCurveUvControlVerticesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepCurveUvNurbControlVertices,
                       SdfValueTypeNames->Double2Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::GetCurveUvVertexCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepCurveUvNurbVertexCount);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::CreateCurveUvVertexCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepCurveUvNurbVertexCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::GetCurveUvOrderAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepCurveUvNurbOrder);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::CreateCurveUvOrderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepCurveUvNurbOrder,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::GetCurveUvKnotsAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepCurveUvNurbKnots);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::CreateCurveUvKnotsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepCurveUvNurbKnots,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::GetCurveUvWeightsAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepCurveUvNurbWeights);
}

UsdAttribute
UsdSolidBrepCurveUvNurbAPI::CreateCurveUvWeightsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepCurveUvNurbWeights,
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
UsdSolidBrepCurveUvNurbAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepCurveUvNurbControlVertices,
        UsdSolidTokens->brepCurveUvNurbVertexCount,
        UsdSolidTokens->brepCurveUvNurbOrder,
        UsdSolidTokens->brepCurveUvNurbKnots,
        UsdSolidTokens->brepCurveUvNurbWeights,
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

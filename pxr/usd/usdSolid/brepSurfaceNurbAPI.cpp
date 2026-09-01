//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepSurfaceNurbAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepSurfaceNurbAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdSolidBrepSurfaceNurbAPI::~UsdSolidBrepSurfaceNurbAPI()
{
}

/* static */
UsdSolidBrepSurfaceNurbAPI
UsdSolidBrepSurfaceNurbAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepSurfaceNurbAPI();
    }
    return UsdSolidBrepSurfaceNurbAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdSolidBrepSurfaceNurbAPI::_GetSchemaKind() const
{
    return UsdSolidBrepSurfaceNurbAPI::schemaKind;
}

/* static */
bool
UsdSolidBrepSurfaceNurbAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdSolidBrepSurfaceNurbAPI>(whyNot);
}

/* static */
UsdSolidBrepSurfaceNurbAPI
UsdSolidBrepSurfaceNurbAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdSolidBrepSurfaceNurbAPI>()) {
        return UsdSolidBrepSurfaceNurbAPI(prim);
    }
    return UsdSolidBrepSurfaceNurbAPI();
}

/* static */
const TfType &
UsdSolidBrepSurfaceNurbAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepSurfaceNurbAPI>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepSurfaceNurbAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepSurfaceNurbAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceControlVerticesAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbControlVertices);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceControlVerticesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbControlVertices,
                       SdfValueTypeNames->Point3dArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceUVertexCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbUVertexCount);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceUVertexCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbUVertexCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceVVertexCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbVVertexCount);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceVVertexCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbVVertexCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceUOrderAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbUOrder);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceUOrderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbUOrder,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceVOrderAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbVOrder);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceVOrderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbVOrder,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceUKnotsAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbUKnots);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceUKnotsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbUKnots,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceVKnotsAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbVKnots);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceVKnotsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbVKnots,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::GetSurfaceWeightsAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepSurfaceNurbWeights);
}

UsdAttribute
UsdSolidBrepSurfaceNurbAPI::CreateSurfaceWeightsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepSurfaceNurbWeights,
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
UsdSolidBrepSurfaceNurbAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepSurfaceNurbControlVertices,
        UsdSolidTokens->brepSurfaceNurbUVertexCount,
        UsdSolidTokens->brepSurfaceNurbVVertexCount,
        UsdSolidTokens->brepSurfaceNurbUOrder,
        UsdSolidTokens->brepSurfaceNurbVOrder,
        UsdSolidTokens->brepSurfaceNurbUKnots,
        UsdSolidTokens->brepSurfaceNurbVKnots,
        UsdSolidTokens->brepSurfaceNurbWeights,
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

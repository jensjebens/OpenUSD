//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include ".//brepArray.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdSolidBrepArray,
        TfType::Bases< UsdGeomGprim > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("BrepArray")
    // to find TfType<UsdSolidBrepArray>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, UsdSolidBrepArray>("BrepArray");
}

/* virtual */
UsdSolidBrepArray::~UsdSolidBrepArray()
{
}

/* static */
UsdSolidBrepArray
UsdSolidBrepArray::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepArray();
    }
    return UsdSolidBrepArray(stage->GetPrimAtPath(path));
}

/* static */
UsdSolidBrepArray
UsdSolidBrepArray::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("BrepArray");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdSolidBrepArray();
    }
    return UsdSolidBrepArray(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind UsdSolidBrepArray::_GetSchemaKind() const
{
    return UsdSolidBrepArray::schemaKind;
}

/* static */
const TfType &
UsdSolidBrepArray::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdSolidBrepArray>();
    return tfType;
}

/* static */
bool 
UsdSolidBrepArray::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdSolidBrepArray::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdSolidBrepArray::GetBrepIntersectTol3dAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepIntersectTol3d);
}

UsdAttribute
UsdSolidBrepArray::CreateBrepIntersectTol3dAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepIntersectTol3d,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetBrepExtentAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepExtent);
}

UsdAttribute
UsdSolidBrepArray::CreateBrepExtentAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepExtent,
                       SdfValueTypeNames->Double3Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetBrepRegionCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->brepRegionCount);
}

UsdAttribute
UsdSolidBrepArray::CreateBrepRegionCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->brepRegionCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetRegionShellCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->regionShellCount);
}

UsdAttribute
UsdSolidBrepArray::CreateRegionShellCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->regionShellCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetRegionTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->regionType);
}

UsdAttribute
UsdSolidBrepArray::CreateRegionTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->regionType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetShellFaceuseCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->shellFaceuseCount);
}

UsdAttribute
UsdSolidBrepArray::CreateShellFaceuseCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->shellFaceuseCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetShellWireEdgeCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->shellWireEdgeCount);
}

UsdAttribute
UsdSolidBrepArray::CreateShellWireEdgeCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->shellWireEdgeCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetShellPointTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->shellPointType);
}

UsdAttribute
UsdSolidBrepArray::CreateShellPointTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->shellPointType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetFaceuseFaceIndexAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->faceuseFaceIndex);
}

UsdAttribute
UsdSolidBrepArray::CreateFaceuseFaceIndexAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->faceuseFaceIndex,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetFaceuseOrientationTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->faceuseOrientationType);
}

UsdAttribute
UsdSolidBrepArray::CreateFaceuseOrientationTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->faceuseOrientationType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetFaceLoopCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->faceLoopCount);
}

UsdAttribute
UsdSolidBrepArray::CreateFaceLoopCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->faceLoopCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetFaceSurfaceTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->faceSurfaceType);
}

UsdAttribute
UsdSolidBrepArray::CreateFaceSurfaceTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->faceSurfaceType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetFaceTrimTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->faceTrimType);
}

UsdAttribute
UsdSolidBrepArray::CreateFaceTrimTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->faceTrimType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetFaceRangeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->faceRange);
}

UsdAttribute
UsdSolidBrepArray::CreateFaceRangeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->faceRange,
                       SdfValueTypeNames->Double2Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetLoopEdgeuseCountAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->loopEdgeuseCount);
}

UsdAttribute
UsdSolidBrepArray::CreateLoopEdgeuseCountAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->loopEdgeuseCount,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetLoopVertexIndexAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->loopVertexIndex);
}

UsdAttribute
UsdSolidBrepArray::CreateLoopVertexIndexAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->loopVertexIndex,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeuseEdgeIndexAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeuseEdgeIndex);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeuseEdgeIndexAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeuseEdgeIndex,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeuseOrientationTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeuseOrientationType);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeuseOrientationTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeuseOrientationType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeuseNextRadialEUIndexAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeuseNextRadialEUIndex);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeuseNextRadialEUIndexAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeuseNextRadialEUIndex,
                       SdfValueTypeNames->UIntArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeuseThisRadialEntryTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeuseThisRadialEntryType);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeuseThisRadialEntryTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeuseThisRadialEntryType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeCurveTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeCurveType);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeCurveTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeCurveType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeRangeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeRange);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeRangeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeRange,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetEdgeVertexIndicesAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->edgeVertexIndices);
}

UsdAttribute
UsdSolidBrepArray::CreateEdgeVertexIndicesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->edgeVertexIndices,
                       SdfValueTypeNames->Int2Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetWireEdgeCurveTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->wireEdgeCurveType);
}

UsdAttribute
UsdSolidBrepArray::CreateWireEdgeCurveTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->wireEdgeCurveType,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetWireEdgeRangeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->wireEdgeRange);
}

UsdAttribute
UsdSolidBrepArray::CreateWireEdgeRangeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->wireEdgeRange,
                       SdfValueTypeNames->DoubleArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetWireEdgeVertexIndicesAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->wireEdgeVertexIndices);
}

UsdAttribute
UsdSolidBrepArray::CreateWireEdgeVertexIndicesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->wireEdgeVertexIndices,
                       SdfValueTypeNames->Int2Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdSolidBrepArray::GetVertexPointTypeAttr() const
{
    return GetPrim().GetAttribute(UsdSolidTokens->vertexPointType);
}

UsdAttribute
UsdSolidBrepArray::CreateVertexPointTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdSolidTokens->vertexPointType,
                       SdfValueTypeNames->TokenArray,
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
UsdSolidBrepArray::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdSolidTokens->brepIntersectTol3d,
        UsdSolidTokens->brepExtent,
        UsdSolidTokens->brepRegionCount,
        UsdSolidTokens->regionShellCount,
        UsdSolidTokens->regionType,
        UsdSolidTokens->shellFaceuseCount,
        UsdSolidTokens->shellWireEdgeCount,
        UsdSolidTokens->shellPointType,
        UsdSolidTokens->faceuseFaceIndex,
        UsdSolidTokens->faceuseOrientationType,
        UsdSolidTokens->faceLoopCount,
        UsdSolidTokens->faceSurfaceType,
        UsdSolidTokens->faceTrimType,
        UsdSolidTokens->faceRange,
        UsdSolidTokens->loopEdgeuseCount,
        UsdSolidTokens->loopVertexIndex,
        UsdSolidTokens->edgeuseEdgeIndex,
        UsdSolidTokens->edgeuseOrientationType,
        UsdSolidTokens->edgeuseNextRadialEUIndex,
        UsdSolidTokens->edgeuseThisRadialEntryType,
        UsdSolidTokens->edgeCurveType,
        UsdSolidTokens->edgeRange,
        UsdSolidTokens->edgeVertexIndices,
        UsdSolidTokens->wireEdgeCurveType,
        UsdSolidTokens->wireEdgeRange,
        UsdSolidTokens->wireEdgeVertexIndices,
        UsdSolidTokens->vertexPointType,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdGeomGprim::GetSchemaAttributeNames(true),
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

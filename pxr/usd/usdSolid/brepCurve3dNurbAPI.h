//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPCURVE3DNURBAPI_H
#define USDSOLID_GENERATED_BREPCURVE3DNURBAPI_H

/// \file usdSolid/brepCurve3dNurbAPI.h

#include "pxr/pxr.h"
#include ".//api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include ".//tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// BREPCURVE3DNURBAPI                                                         //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepCurve3dNurbAPI
///
/// Associated and packed NURB curve descriptions of brep:edges and brep:wireEdges defined by 3d BSpline curves.
/// 
/// This class varies from the UsdGeomNurbCurves primarily in having double precision control vertices. 
/// 
/// This schema is analagous to NURB Curves in packages like Maya and Houdini, often used for interchange of rigging
/// and modeling curves. We require 'numSegments + 2 * degree + 1' knots (2 more than maya does). This is to be more
/// consistent with RenderMan's NURB patch specification. 
///
class UsdSolidBrepCurve3dNurbAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdSolidBrepCurve3dNurbAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdSolidBrepCurve3dNurbAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "brep:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepCurve3dNurbAPI(
        const UsdPrim& prim=UsdPrim(), const TfToken &name=TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    { }

    /// Construct a UsdSolidBrepCurve3dNurbAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdSolidBrepCurve3dNurbAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdSolidBrepCurve3dNurbAPI(
        const UsdSchemaBase& schemaObj, const TfToken &name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    { }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepCurve3dNurbAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSOLID_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes for a given instance name.  Does not
    /// include attributes that may be authored by custom/extended methods of
    /// the schemas involved. The names returned will have the proper namespace
    /// prefix.
    USDSOLID_API
    static TfTokenVector
    GetSchemaAttributeNames(bool includeInherited, const TfToken &instanceName);

    /// Returns the name of this multiple-apply schema instance
    TfToken GetName() const {
        return _GetInstanceName();
    }

    /// Return a UsdSolidBrepCurve3dNurbAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.brep:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdSolidBrepCurve3dNurbAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepCurve3dNurbAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Return a UsdSolidBrepCurve3dNurbAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdSolidBrepCurve3dNurbAPI(prim, name);
    USDSOLID_API
    static UsdSolidBrepCurve3dNurbAPI
    Get(const UsdPrim &prim, const TfToken &name);

    /// Return a vector of all named instances of UsdSolidBrepCurve3dNurbAPI on the 
    /// given \p prim.
    USDSOLID_API
    static std::vector<UsdSolidBrepCurve3dNurbAPI>
    GetAll(const UsdPrim &prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of BrepCurve3dNurbAPI.
    USDSOLID_API
    static bool
    IsSchemaPropertyBaseName(const TfToken &baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// BrepCurve3dNurbAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
    USDSOLID_API
    static bool
    IsBrepCurve3dNurbAPIPath(const SdfPath &path, TfToken *name);

    /// Returns true if this <b>multiple-apply</b> API schema can be applied,
    /// with the given instance name, \p name, to the given \p prim. If this 
    /// schema can not be a applied the prim, this returns false and, if 
    /// provided, populates \p whyNot with the reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDSOLID_API
    static bool 
    CanApply(const UsdPrim &prim, const TfToken &name, 
             std::string *whyNot=nullptr);

    /// Applies this <b>multiple-apply</b> API schema to the given \p prim 
    /// along with the given instance name, \p name. 
    /// 
    /// This information is stored by adding "BrepCurve3dNurbAPI:<i>name</i>" 
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token 
    /// 'BrepCurve3dNurbAPI:instance1' is added to 'apiSchemas'.
    /// 
    /// \return A valid UsdSolidBrepCurve3dNurbAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepCurve3dNurbAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for 
    /// conditions resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDSOLID_API
    static UsdSolidBrepCurve3dNurbAPI 
    Apply(const UsdPrim &prim, const TfToken &name);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDSOLID_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDSOLID_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDSOLID_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DCONTROLVERTICES 
    // --------------------------------------------------------------------- //
    /// packed 3d control vertices for all edge or wireEdge NurbCurves.
    /// size() = SUM_ii(curve3dVertexCount[ii]). 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] curve3d:nurb:controlVertices` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dControlVerticesAttr() const;

    /// See GetCurve3dControlVerticesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dControlVerticesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DVERTEXCOUNT 
    // --------------------------------------------------------------------- //
    /// Curve3d_ii's number of control vertices.
    /// size() = number of this instance's 3dNURB curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] curve3d:nurb:vertexCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dVertexCountAttr() const;

    /// See GetCurve3dVertexCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dVertexCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DORDER 
    // --------------------------------------------------------------------- //
    /// Curve3d_ii's order. Where, Order = Degree + 1.
    /// Order must be positive and is equal to the degree of the polynomial basis to be evaluated, plus 1.
    /// Its value for the 'ii'th curve must be less than or equal to vertexCount[ii].
    /// size() = number of this instance's 3dNURB curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] curve3d:nurb:order` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dOrderAttr() const;

    /// See GetCurve3dOrderAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dOrderAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DKNOTS 
    // --------------------------------------------------------------------- //
    /// Curve3d_ii's knot vector providing curve parameterization.
    /// The length of the slice of the array for the iith curve must be ( vertexCount[ii] + order[ii] ), and its entries
    /// must take on non-decreasing values. Knots are listed in multiplicity, e.g. [0, 0, 1, 1].
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] curve3d:nurb:knots` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dKnotsAttr() const;

    /// See GetCurve3dKnotsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dKnotsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DWEIGHTS 
    // --------------------------------------------------------------------- //
    /// Curve3d_ii's "w" weight components for each control vertex. 
    /// Must be the same length as the curveControlVertices attribute. Weights must be positive, w>0. 
    /// \note Some DCC's pre-weight the \em points, but in this schema, \em points are not pre-weighted.        
    /// size() = curve3dControlVertices.size() 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] curve3d:nurb:weights` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dWeightsAttr() const;

    /// See GetCurve3dWeightsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dWeightsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

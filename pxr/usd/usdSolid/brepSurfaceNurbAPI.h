//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPSURFACENURBAPI_H
#define USDSOLID_GENERATED_BREPSURFACENURBAPI_H

/// \file usdSolid/brepSurfaceNurbAPI.h

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
// BREPSURFACENURBAPI                                                         //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepSurfaceNurbAPI
///
/// Associated and packed 3d NURBs Surface descriptions of brep:faces defined by 3d BSpline surfaces.
/// 
/// These attributes vary from the UsdGeomNurbPatch primarily in having double precision control vertices. 
/// 
/// The encoding mostly follows that of RiNuPatch and RiTrimCurve:
/// https://renderman.pixar.com/resources/current/RenderMan/geometricPrimitives.html#rinupatch , with some minor
/// renaming and coalescing for clarity.
/// 
/// The layout of control vertices in the \em points attribute is row-major with U considered rows, and V columns.
/// 
/// The authored points, orders, knots, weights, and ranges are all that is required to render the nurbs patch.
/// 
///
class UsdSolidBrepSurfaceNurbAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdSolidBrepSurfaceNurbAPI on UsdPrim \p prim .
    /// Equivalent to UsdSolidBrepSurfaceNurbAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepSurfaceNurbAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdSolidBrepSurfaceNurbAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdSolidBrepSurfaceNurbAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdSolidBrepSurfaceNurbAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepSurfaceNurbAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSOLID_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdSolidBrepSurfaceNurbAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdSolidBrepSurfaceNurbAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepSurfaceNurbAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
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
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "BrepSurfaceNurbAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdSolidBrepSurfaceNurbAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepSurfaceNurbAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDSOLID_API
    static UsdSolidBrepSurfaceNurbAPI 
    Apply(const UsdPrim &prim);

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
    // SURFACECONTROLVERTICES 
    // --------------------------------------------------------------------- //
    /// packed control vertices of the all Nurb Surfaces.
    /// The layout is row-major with U considered rows, and V columns.
    /// size() = SUM_ii(uVertexCount[ii] * vVertexCount[ii]). 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] brep:surface:nurb:controlVertices` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceControlVerticesAttr() const;

    /// See GetSurfaceControlVerticesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceControlVerticesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEUVERTEXCOUNT 
    // --------------------------------------------------------------------- //
    /// surface_ii's number of control vertices in U dir.
    /// size() = number of NURB surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] brep:surface:nurb:uVertexCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceUVertexCountAttr() const;

    /// See GetSurfaceUVertexCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceUVertexCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEVVERTEXCOUNT 
    // --------------------------------------------------------------------- //
    /// surface_ii's number of control vertices in V dir.
    /// size() = number of NURB surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] brep:surface:nurb:vVertexCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceVVertexCountAttr() const;

    /// See GetSurfaceVVertexCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceVVertexCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEUORDER 
    // --------------------------------------------------------------------- //
    /// Order in the U direction.
    /// Order must be positive and is equal to the degree of the polynomial basis to be evaluated, plus 1.
    /// size() = surface_ii's order in the U dir. Where, Order = Degree + 1.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] brep:surface:nurb:uOrder` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceUOrderAttr() const;

    /// See GetSurfaceUOrderAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceUOrderAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEVORDER 
    // --------------------------------------------------------------------- //
    /// Order in the V direction.
    /// Order must be positive and is equal to the degree of the polynomial basis to be evaluated, plus 1.
    /// size() = surface_ii's order in the V dir. Where, Order = Degree + 1.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] brep:surface:nurb:vOrder` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceVOrderAttr() const;

    /// See GetSurfaceVOrderAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceVOrderAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEUKNOTS 
    // --------------------------------------------------------------------- //
    /// surface_ii's knot vector in U direction providing U parameterization.
    /// The length of the slice of the array for the iith surface must be ( uVertexCount[ii] + uOrder[ii] ), and its entries
    /// must take on non-decreasing values.  Knots are listed in multiplicity, e.g. [0, 0, 1, 1].
    /// size() = SUM_ii(surfaceUVertexCount[ii]) + SUM_ii(surfaceUOrder[ii]). 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:nurb:uKnots` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceUKnotsAttr() const;

    /// See GetSurfaceUKnotsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceUKnotsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEVKNOTS 
    // --------------------------------------------------------------------- //
    /// surface_ii's knot vector in V direction providing V parameterization.
    /// The length of the slice of the array for the iith surface must be ( vVertexCount[ii] + vOrder[ii] ), and its entries
    /// must take on non-decreasing values.  Knots are listed in multiplicity, e.g. [0, 0, 1, 1].
    /// size() = SUM_ii(surfaceVVertexCount) + SUM_ii(surfaceVOrder[ii]). 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:nurb:vKnots` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceVKnotsAttr() const;

    /// See GetSurfaceVKnotsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceVKnotsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEWEIGHTS 
    // --------------------------------------------------------------------- //
    /// surface_ii's "w" weight components for each control vertex. Must be the same length as the surfaceControlVertices attribute. 
    /// All weights must be positive, w>0. 
    /// \note Some DCC's pre-weight the \em points, but in this schema, \em points are not pre-weighted. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:nurb:weights` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceWeightsAttr() const;

    /// See GetSurfaceWeightsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceWeightsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

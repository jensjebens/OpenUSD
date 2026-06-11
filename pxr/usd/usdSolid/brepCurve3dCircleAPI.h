//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPCURVE3DCIRCLEAPI_H
#define USDSOLID_GENERATED_BREPCURVE3DCIRCLEAPI_H

/// \file usdSolid/brepCurve3dCircleAPI.h

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
// BREPCURVE3DCIRCLEAPI                                                       //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepCurve3dCircleAPI
///
/// Associated and packed analytic 3d circle descriptions of brep:edges and brep:wireEdges.
/// 
/// A circle is defined by a center, an axis (normal to the plane of the circle),
/// a reference direction (in the plane of the circle), and a radius.
/// The parameterization follows the STEP convention:
/// C(t) = center + radius * cos(t) * refDirection
/// + radius * sin(t) * (axis x refDirection)
/// where t is the angular parameter in radians.
/// The edge:range attribute provides the parameter bounds {tMin, tMax} in radians.
/// 
/// Both PRC (A3DCrvCircleData) and SMLib (SmCircle) define circles with an axis placement
/// and a radius. PRC uses radians; SMLib uses degrees internally but this schema
/// normalizes to radians.
/// 
///
class UsdSolidBrepCurve3dCircleAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdSolidBrepCurve3dCircleAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdSolidBrepCurve3dCircleAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "brep:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepCurve3dCircleAPI(
        const UsdPrim& prim=UsdPrim(), const TfToken &name=TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    { }

    /// Construct a UsdSolidBrepCurve3dCircleAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdSolidBrepCurve3dCircleAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdSolidBrepCurve3dCircleAPI(
        const UsdSchemaBase& schemaObj, const TfToken &name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    { }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepCurve3dCircleAPI();

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

    /// Return a UsdSolidBrepCurve3dCircleAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.brep:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdSolidBrepCurve3dCircleAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepCurve3dCircleAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Return a UsdSolidBrepCurve3dCircleAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdSolidBrepCurve3dCircleAPI(prim, name);
    USDSOLID_API
    static UsdSolidBrepCurve3dCircleAPI
    Get(const UsdPrim &prim, const TfToken &name);

    /// Return a vector of all named instances of UsdSolidBrepCurve3dCircleAPI on the 
    /// given \p prim.
    USDSOLID_API
    static std::vector<UsdSolidBrepCurve3dCircleAPI>
    GetAll(const UsdPrim &prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of BrepCurve3dCircleAPI.
    USDSOLID_API
    static bool
    IsSchemaPropertyBaseName(const TfToken &baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// BrepCurve3dCircleAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
    USDSOLID_API
    static bool
    IsBrepCurve3dCircleAPIPath(const SdfPath &path, TfToken *name);

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
    /// This information is stored by adding "BrepCurve3dCircleAPI:<i>name</i>" 
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token 
    /// 'BrepCurve3dCircleAPI:instance1' is added to 'apiSchemas'.
    /// 
    /// \return A valid UsdSolidBrepCurve3dCircleAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepCurve3dCircleAPI object is returned upon 
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
    static UsdSolidBrepCurve3dCircleAPI 
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
    // CURVE3DCIRCLECENTER 
    // --------------------------------------------------------------------- //
    /// packed center points for all circle curves.
    /// size() = number of this instance's 3d circle curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] curve3d:circle:center` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dCircleCenterAttr() const;

    /// See GetCurve3dCircleCenterAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dCircleCenterAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DCIRCLEAXIS 
    // --------------------------------------------------------------------- //
    /// packed axis directions (unit vector, normal to circle plane) for all circle curves.
    /// Defines the plane of the circle. Z = axis.
    /// size() = number of this instance's 3d circle curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] curve3d:circle:axis` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dCircleAxisAttr() const;

    /// See GetCurve3dCircleAxisAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dCircleAxisAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DCIRCLEREFDIRECTION 
    // --------------------------------------------------------------------- //
    /// packed reference directions (unit vector, in circle plane) for all circle curves.
    /// Defines where t = 0 on the circle. Must be orthogonal to axis. X = refDirection.
    /// size() = number of this instance's 3d circle curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] curve3d:circle:refDirection` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dCircleRefDirectionAttr() const;

    /// See GetCurve3dCircleRefDirectionAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dCircleRefDirectionAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DCIRCLERADIUS 
    // --------------------------------------------------------------------- //
    /// packed radii of all circle curves. Must be positive.
    /// size() = number of this instance's 3d circle curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] curve3d:circle:radius` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dCircleRadiusAttr() const;

    /// See GetCurve3dCircleRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dCircleRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

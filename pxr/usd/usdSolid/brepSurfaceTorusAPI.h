//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPSURFACETORUSAPI_H
#define USDSOLID_GENERATED_BREPSURFACETORUSAPI_H

/// \file usdSolid/brepSurfaceTorusAPI.h

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
// BREPSURFACETORUSAPI                                                        //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepSurfaceTorusAPI
///
/// Associated analytic 3d torus surface descriptions of brep:faces.
/// 
/// A torus is defined by a position (origin), an axis direction, a reference direction,
/// a major radius, and a minor radius. The parameterization follows the STEP convention:
/// S(u,v) = origin + (majorRadius + minorRadius * cos(v)) * cos(u) * refDirection
/// + (majorRadius + minorRadius * cos(v)) * sin(u) * (axis x refDirection)
/// + minorRadius * sin(v) * axis
/// where u is the major (sweep) angle in radians and v is the minor (tube) angle in radians.
/// The surface normal points outward.
/// The face:range attribute provides the UV trim domain bounds in radians.
/// 
/// Both PRC and SMLib define the torus with an axis placement, major radius, and minor radius.
/// PRC uses radians for both angular parameters; SMLib (SmTorus) uses degrees internally
/// but this schema normalizes to radians.
/// 
///
class UsdSolidBrepSurfaceTorusAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdSolidBrepSurfaceTorusAPI on UsdPrim \p prim .
    /// Equivalent to UsdSolidBrepSurfaceTorusAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepSurfaceTorusAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdSolidBrepSurfaceTorusAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdSolidBrepSurfaceTorusAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdSolidBrepSurfaceTorusAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepSurfaceTorusAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSOLID_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdSolidBrepSurfaceTorusAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdSolidBrepSurfaceTorusAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepSurfaceTorusAPI
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
    /// This information is stored by adding "BrepSurfaceTorusAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdSolidBrepSurfaceTorusAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepSurfaceTorusAPI object is returned upon 
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
    static UsdSolidBrepSurfaceTorusAPI 
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
    // SURFACETORUSORIGIN 
    // --------------------------------------------------------------------- //
    /// packed center points of all torus surfaces.
    /// size() = number of torus surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] brep:surface:torus:origin` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceTorusOriginAttr() const;

    /// See GetSurfaceTorusOriginAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceTorusOriginAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACETORUSAXIS 
    // --------------------------------------------------------------------- //
    /// packed axis direction (unit vector) for all torus surfaces.
    /// Defines the symmetry axis of the torus. The minor circle is swept around this axis.
    /// size() = number of torus surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] brep:surface:torus:axis` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceTorusAxisAttr() const;

    /// See GetSurfaceTorusAxisAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceTorusAxisAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACETORUSREFDIRECTION 
    // --------------------------------------------------------------------- //
    /// packed reference direction (unit vector, perpendicular to axis) for all torus surfaces.
    /// Defines where U = 0 for the major sweep angle. Must be orthogonal to axis.
    /// size() = number of torus surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] brep:surface:torus:refDirection` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceTorusRefDirectionAttr() const;

    /// See GetSurfaceTorusRefDirectionAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceTorusRefDirectionAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACETORUSMAJORRADIUS 
    // --------------------------------------------------------------------- //
    /// packed major radii of all torus surfaces. Must be positive.
    /// Distance from the torus center to the center of the tube cross-section.
    /// size() = number of torus surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:torus:majorRadius` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceTorusMajorRadiusAttr() const;

    /// See GetSurfaceTorusMajorRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceTorusMajorRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACETORUSMINORRADIUS 
    // --------------------------------------------------------------------- //
    /// packed minor radii of all torus surfaces. Must be positive.
    /// Radius of the tube cross-section.
    /// size() = number of torus surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:torus:minorRadius` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceTorusMinorRadiusAttr() const;

    /// See GetSurfaceTorusMinorRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceTorusMinorRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

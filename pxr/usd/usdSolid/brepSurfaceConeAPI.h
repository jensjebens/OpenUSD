//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPSURFACECONEAPI_H
#define USDSOLID_GENERATED_BREPSURFACECONEAPI_H

/// \file usdSolid/brepSurfaceConeAPI.h

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
// BREPSURFACECONEAPI                                                         //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepSurfaceConeAPI
///
/// Associated analytic 3d cone surface descriptions of brep:faces.
/// 
/// A cone is defined by a position (origin), an axis direction, a reference direction,
/// a radius at the origin, and a semi-angle. The parameterization follows the STEP convention:
/// R(v)   = radius + v * tan(semiAngle)
/// S(u,v) = origin + R(v) * cos(u) * refDirection
/// + R(v) * sin(u) * (axis x refDirection)
/// + v * axis
/// where u is the angular parameter in radians, v is the linear height parameter,
/// and semiAngle is the half-angle between the axis and the cone surface.
/// When semiAngle is 0, the cone degenerates to a cylinder.
/// The surface normal points outward (away from the axis).
/// The face:range attribute provides the UV trim domain bounds in radians (u) and linear units (v).
/// 
/// Convention note (v-parameter): this v is the AXIAL height (radius grows as
/// tan(semiAngle), axial term + v * axis), matching the STEP/PRC convention. It
/// DIFFERS from OpenCascade's Geom_ConicalSurface, whose V is the SLANT distance
/// along the generatrix (radius grows as sin(semiAngle), axial as cos(semiAngle)).
/// A consumer that maps this surface onto OCCT must rescale V_occt = v / cos(semiAngle);
/// the same applies to face:range v. Authored brep:curveUv pcurves, by contrast, are
/// expressed in the consuming surface's native parameterization (i.e. OCCT slant for an
/// OCCT-based tessellator), not this axial v.
/// 
/// Both PRC and SMLib define the cone with an axis placement, base radius, and semi-angle.
/// PRC uses radians for the angular parameter; SMLib (SmCone) uses degrees internally
/// but this schema normalizes to radians.
/// 
///
class UsdSolidBrepSurfaceConeAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdSolidBrepSurfaceConeAPI on UsdPrim \p prim .
    /// Equivalent to UsdSolidBrepSurfaceConeAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepSurfaceConeAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdSolidBrepSurfaceConeAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdSolidBrepSurfaceConeAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdSolidBrepSurfaceConeAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepSurfaceConeAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSOLID_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdSolidBrepSurfaceConeAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdSolidBrepSurfaceConeAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepSurfaceConeAPI
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
    /// This information is stored by adding "BrepSurfaceConeAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdSolidBrepSurfaceConeAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepSurfaceConeAPI object is returned upon 
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
    static UsdSolidBrepSurfaceConeAPI 
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
    // SURFACECONEORIGIN 
    // --------------------------------------------------------------------- //
    /// packed origin points (on the cone axis at the base circle) for all cone surfaces.
    /// size() = number of cone surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] brep:surface:cone:origin` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceConeOriginAttr() const;

    /// See GetSurfaceConeOriginAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceConeOriginAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACECONEAXIS 
    // --------------------------------------------------------------------- //
    /// packed axis direction (unit vector) for all cone surfaces.
    /// Defines the V parameterization direction. The cone opens in the positive axis direction
    /// when semiAngle is positive.
    /// size() = number of cone surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] brep:surface:cone:axis` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceConeAxisAttr() const;

    /// See GetSurfaceConeAxisAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceConeAxisAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACECONEREFDIRECTION 
    // --------------------------------------------------------------------- //
    /// packed reference direction (unit vector, perpendicular to axis) for all cone surfaces.
    /// Defines where U = 0 on the circular cross-section. Must be orthogonal to axis.
    /// size() = number of cone surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] brep:surface:cone:refDirection` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceConeRefDirectionAttr() const;

    /// See GetSurfaceConeRefDirectionAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceConeRefDirectionAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACECONERADIUS 
    // --------------------------------------------------------------------- //
    /// packed base radii (radius at v=0, at the origin) of all cone surfaces. Must be non-negative.
    /// size() = number of cone surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:cone:radius` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceConeRadiusAttr() const;

    /// See GetSurfaceConeRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceConeRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACECONESEMIANGLE 
    // --------------------------------------------------------------------- //
    /// packed semi-angles in radians for all cone surfaces.
    /// The half-angle between the cone axis and the cone surface.
    /// Range: (-pi/2, pi/2). Positive means radius increases with positive V.
    /// size() = number of cone surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:surface:cone:semiAngle` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfaceConeSemiAngleAttr() const;

    /// See GetSurfaceConeSemiAngleAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfaceConeSemiAngleAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPSURFACEPLANEAPI_H
#define USDSOLID_GENERATED_BREPSURFACEPLANEAPI_H

/// \file usdSolid/brepSurfacePlaneAPI.h

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
// BREPSURFACEPLANEAPI                                                        //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepSurfacePlaneAPI
///
/// Associated analytic 3d plane surface descriptions of brep:faces.
/// 
/// A plane is defined by a position (origin), a reference direction (X axis), and a normal direction (Z axis).
/// The Y axis is implied as Z cross X (forming a right-handed frame after orthonormalization).
/// The parameterization follows the STEP convention:
/// S(u,v) = origin + u * refDirection + v * (axis x refDirection)
/// where u and v are unbounded real parameters.
/// The surface normal equals the axis direction.
/// The face:range attribute provides the UV trim domain bounds for each plane face.
/// 
/// This definition is compatible with both PRC (A3DSurfPlaneData) and SMLib (SmPlane)
/// which both use an axis placement (origin, X axis, Z axis) to define the plane.
/// Note: SMLib additionally stores a UV scale factor that scales the U and V directions
/// independently. In this schema, UV scale is always 1.0; any scaling is absorbed into
/// the face:range domain bounds.
/// 
///
class UsdSolidBrepSurfacePlaneAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdSolidBrepSurfacePlaneAPI on UsdPrim \p prim .
    /// Equivalent to UsdSolidBrepSurfacePlaneAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepSurfacePlaneAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdSolidBrepSurfacePlaneAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdSolidBrepSurfacePlaneAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdSolidBrepSurfacePlaneAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepSurfacePlaneAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSOLID_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdSolidBrepSurfacePlaneAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdSolidBrepSurfacePlaneAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepSurfacePlaneAPI
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
    /// This information is stored by adding "BrepSurfacePlaneAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdSolidBrepSurfacePlaneAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepSurfacePlaneAPI object is returned upon 
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
    static UsdSolidBrepSurfacePlaneAPI 
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
    // SURFACEPLANEORIGIN 
    // --------------------------------------------------------------------- //
    /// packed origin points of all plane surfaces.
    /// S(0,0) evaluates to this point.
    /// size() = number of plane surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] brep:surface:plane:origin` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfacePlaneOriginAttr() const;

    /// See GetSurfacePlaneOriginAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfacePlaneOriginAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEPLANEAXIS 
    // --------------------------------------------------------------------- //
    /// packed normal direction (unit vector) for all plane surfaces.
    /// Defines the surface normal direction. Z = axis.
    /// size() = number of plane surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] brep:surface:plane:axis` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfacePlaneAxisAttr() const;

    /// See GetSurfacePlaneAxisAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfacePlaneAxisAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SURFACEPLANEREFDIRECTION 
    // --------------------------------------------------------------------- //
    /// packed reference direction (unit vector, in the plane) for all plane surfaces.
    /// Defines where U = 0. Must be orthogonal to axis. X = refDirection.
    /// size() = number of plane surfaces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] brep:surface:plane:refDirection` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetSurfacePlaneRefDirectionAttr() const;

    /// See GetSurfacePlaneRefDirectionAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateSurfacePlaneRefDirectionAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

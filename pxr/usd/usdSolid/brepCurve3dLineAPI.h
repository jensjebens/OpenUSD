//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPCURVE3DLINEAPI_H
#define USDSOLID_GENERATED_BREPCURVE3DLINEAPI_H

/// \file usdSolid/brepCurve3dLineAPI.h

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
// BREPCURVE3DLINEAPI                                                         //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepCurve3dLineAPI
///
/// Associated and packed analytic 3d line descriptions of brep:edges and brep:wireEdges.
/// 
/// A line is defined by a point on the line and a unit direction vector.
/// The parameterization follows the STEP/PRC convention:
/// C(t) = origin + t * direction
/// where t is the curve parameter and direction is a unit vector.
/// The edge:range attribute provides the parameter bounds {tMin, tMax} for each line edge.
/// 
/// Both PRC (A3DCrvLineData) and SMLib (SmLine) define lines with a point and direction.
/// SMLib additionally stores a scale factor (curve speed); in this schema the speed is
/// always 1.0, meaning distance along the line equals the parameter change.
/// 
///
class UsdSolidBrepCurve3dLineAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdSolidBrepCurve3dLineAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdSolidBrepCurve3dLineAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "brep:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepCurve3dLineAPI(
        const UsdPrim& prim=UsdPrim(), const TfToken &name=TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    { }

    /// Construct a UsdSolidBrepCurve3dLineAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdSolidBrepCurve3dLineAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdSolidBrepCurve3dLineAPI(
        const UsdSchemaBase& schemaObj, const TfToken &name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    { }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepCurve3dLineAPI();

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

    /// Return a UsdSolidBrepCurve3dLineAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.brep:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdSolidBrepCurve3dLineAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepCurve3dLineAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Return a UsdSolidBrepCurve3dLineAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdSolidBrepCurve3dLineAPI(prim, name);
    USDSOLID_API
    static UsdSolidBrepCurve3dLineAPI
    Get(const UsdPrim &prim, const TfToken &name);

    /// Return a vector of all named instances of UsdSolidBrepCurve3dLineAPI on the 
    /// given \p prim.
    USDSOLID_API
    static std::vector<UsdSolidBrepCurve3dLineAPI>
    GetAll(const UsdPrim &prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of BrepCurve3dLineAPI.
    USDSOLID_API
    static bool
    IsSchemaPropertyBaseName(const TfToken &baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// BrepCurve3dLineAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
    USDSOLID_API
    static bool
    IsBrepCurve3dLineAPIPath(const SdfPath &path, TfToken *name);

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
    /// This information is stored by adding "BrepCurve3dLineAPI:<i>name</i>" 
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token 
    /// 'BrepCurve3dLineAPI:instance1' is added to 'apiSchemas'.
    /// 
    /// \return A valid UsdSolidBrepCurve3dLineAPI object is returned upon success. 
    /// An invalid (or empty) UsdSolidBrepCurve3dLineAPI object is returned upon 
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
    static UsdSolidBrepCurve3dLineAPI 
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
    // CURVE3DLINEORIGIN 
    // --------------------------------------------------------------------- //
    /// packed origin points for all line curves.
    /// C(0) evaluates to this point.
    /// size() = number of this instance's 3d line curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform point3d[] curve3d:line:origin` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Point3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dLineOriginAttr() const;

    /// See GetCurve3dLineOriginAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dLineOriginAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CURVE3DLINEDIRECTION 
    // --------------------------------------------------------------------- //
    /// packed direction vectors (unit length) for all line curves.
    /// size() = number of this instance's 3d line curves. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform vector3d[] curve3d:line:direction` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Vector3dArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetCurve3dLineDirectionAttr() const;

    /// See GetCurve3dLineDirectionAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateCurve3dLineDirectionAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

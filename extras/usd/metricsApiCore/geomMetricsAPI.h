//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDMETRICSAPI_GENERATED_GEOMMETRICSAPI_H
#define USDMETRICSAPI_GENERATED_GEOMMETRICSAPI_H

/// \file usdMetricsApi/geomMetricsAPI.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "./tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// GEOMMETRICSAPI                                                             //
// -------------------------------------------------------------------------- //

/// \class UsdMetricsGeomMetricsAPI
///
/// Applied API schema declaring the spatial unit context for a
/// prim's subtree.
/// 
/// When applied to a prim, metersPerUnit and upAxis specify the unit
/// system in which all spatial attribute values on this prim and its
/// descendants are expressed, until overridden by a descendant prim
/// that also applies GeomMetricsAPI.
/// 
/// Consumers should call GetEffectiveMetersPerUnit() which walks
/// ancestor prims to find the nearest declaration, falling back to
/// stage-level metersPerUnit metadata, then to the USD default (0.01,
/// centimeters).
/// 
/// This schema is designed to align with the MetricsAPI direction
/// proposed in PR #45 (Revise Use of Layer Metadata).
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdMetricsTokens.
/// So to set an attribute to the value "rightHanded", use UsdMetricsTokens->rightHanded
/// as the value.
///
class UsdMetricsGeomMetricsAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdMetricsGeomMetricsAPI on UsdPrim \p prim .
    /// Equivalent to UsdMetricsGeomMetricsAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdMetricsGeomMetricsAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdMetricsGeomMetricsAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdMetricsGeomMetricsAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdMetricsGeomMetricsAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDMETRICSAPI_API
    virtual ~UsdMetricsGeomMetricsAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDMETRICSAPI_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdMetricsGeomMetricsAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdMetricsGeomMetricsAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDMETRICSAPI_API
    static UsdMetricsGeomMetricsAPI
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
    USDMETRICSAPI_API
    static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "GeomMetricsAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdMetricsGeomMetricsAPI object is returned upon success. 
    /// An invalid (or empty) UsdMetricsGeomMetricsAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDMETRICSAPI_API
    static UsdMetricsGeomMetricsAPI 
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDMETRICSAPI_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDMETRICSAPI_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDMETRICSAPI_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // METERSPERUNIT 
    // --------------------------------------------------------------------- //
    /// Scale factor from the prim's linear unit to meters.
    /// 0.001 = millimeters, 0.01 = centimeters, 1.0 = meters.
    /// Inherited by descendants until overridden.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `double metrics:metersPerUnit` |
    /// | C++ Type | double |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Double |
    USDMETRICSAPI_API
    UsdAttribute GetMetersPerUnitAttr() const;

    /// See GetMetersPerUnitAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDMETRICSAPI_API
    UsdAttribute CreateMetersPerUnitAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // UPAXIS 
    // --------------------------------------------------------------------- //
    /// Vertical axis convention for the prim's subtree.
    /// Inherited by descendants until overridden.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `token metrics:upAxis` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref UsdMetricsTokens "Allowed Values" | Y, Z |
    USDMETRICSAPI_API
    UsdAttribute GetUpAxisAttr() const;

    /// See GetUpAxisAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDMETRICSAPI_API
    UsdAttribute CreateUpAxisAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

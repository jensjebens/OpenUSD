//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDMETRICSAPI_GENERATED_PHYSICSMETRICSAPI_H
#define USDMETRICSAPI_GENERATED_PHYSICSMETRICSAPI_H

/// \file usdMetricsApi/physicsMetricsAPI.h

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
// PHYSICSMETRICSAPI                                                          //
// -------------------------------------------------------------------------- //

/// \class UsdMetricsPhysicsMetricsAPI
///
/// Applied API schema declaring the mass unit context for a
/// prim's subtree.
/// 
/// When applied to a prim, kilogramsPerUnit specifies the mass unit in
/// which mass-bearing attribute values on this prim and its descendants
/// are expressed, until overridden by a descendant prim that also
/// applies PhysicsMetricsAPI.
/// 
/// Typically applied alongside GeomMetricsAPI on the same prim.
///
class UsdMetricsPhysicsMetricsAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdMetricsPhysicsMetricsAPI on UsdPrim \p prim .
    /// Equivalent to UsdMetricsPhysicsMetricsAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdMetricsPhysicsMetricsAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdMetricsPhysicsMetricsAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdMetricsPhysicsMetricsAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdMetricsPhysicsMetricsAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDMETRICSAPI_API
    virtual ~UsdMetricsPhysicsMetricsAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDMETRICSAPI_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdMetricsPhysicsMetricsAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdMetricsPhysicsMetricsAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDMETRICSAPI_API
    static UsdMetricsPhysicsMetricsAPI
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
    /// This information is stored by adding "PhysicsMetricsAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdMetricsPhysicsMetricsAPI object is returned upon success. 
    /// An invalid (or empty) UsdMetricsPhysicsMetricsAPI object is returned upon 
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
    static UsdMetricsPhysicsMetricsAPI 
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
    // KILOGRAMSPERUNIT 
    // --------------------------------------------------------------------- //
    /// Scale factor from the prim's mass unit to kilograms.
    /// 1.0 = kilograms, 0.001 = grams.
    /// Inherited by descendants until overridden.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `double metrics:kilogramsPerUnit` |
    /// | C++ Type | double |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Double |
    USDMETRICSAPI_API
    UsdAttribute GetKilogramsPerUnitAttr() const;

    /// See GetKilogramsPerUnitAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDMETRICSAPI_API
    UsdAttribute CreateKilogramsPerUnitAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

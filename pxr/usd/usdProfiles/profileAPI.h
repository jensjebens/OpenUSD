//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDPROFILES_GENERATED_PROFILEAPI_H
#define USDPROFILES_GENERATED_PROFILEAPI_H

/// \file usdProfiles/profileAPI.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
//
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
//
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "pxr/usd/usdProfiles/api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdProfiles/tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// PROFILEAPI                                                                  //
// -------------------------------------------------------------------------- //

/// \class UsdProfilesProfileAPI
///
/// Applied API schema for declaring that a prim (and its descendants)
/// conform to a specific USD Profile — a named set of capabilities.
///
/// A Profile is a tagged node in a capability DAG representing a coherent
/// set of functionality. Applying ProfileAPI to a prim declares that the
/// subtree rooted at that prim satisfies the capabilities required by
/// the named profile.
///
/// Resolution: If no ProfileAPI is authored on a prim, the nearest
/// ancestor's ProfileAPI applies. Capability requirements do not
/// propagate upward through hierarchies.
///
/// See: https://github.com/PixarAnimationStudios/OpenUSD-proposals/tree/main/proposals/profiles
///
class UsdProfilesProfileAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdProfilesProfileAPI on UsdPrim \p prim .
    /// Equivalent to UsdProfilesProfileAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdProfilesProfileAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdProfilesProfileAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdProfilesProfileAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdProfilesProfileAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDPROFILES_API
    virtual ~UsdProfilesProfileAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDPROFILES_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdProfilesProfileAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdProfilesProfileAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDPROFILES_API
    static UsdProfilesProfileAPI
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
    USDPROFILES_API
    static bool
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "ProfileAPI" to the
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    ///
    /// \return A valid UsdProfilesProfileAPI object is returned upon success.
    /// An invalid (or empty) UsdProfilesProfileAPI object is returned upon
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions
    /// resulting in failure.
    ///
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDPROFILES_API
    static UsdProfilesProfileAPI
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDPROFILES_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDPROFILES_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDPROFILES_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // PROFILE
    // --------------------------------------------------------------------- //
    /// The profile identifier this prim conforms to.
    /// Uses reverse domain notation, e.g.:
    /// - com.nvidia.simready.prop_robotics_neutral
    /// - usd.core.v25_05
    /// - aousd.interchange.v1_0
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `token profiles:profile` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    USDPROFILES_API
    UsdAttribute GetProfileAttr() const;

    /// See GetProfileAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDPROFILES_API
    UsdAttribute CreateProfileAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CAPABILITIES
    // --------------------------------------------------------------------- //
    /// Explicit list of capability identifiers this prim requires,
    /// beyond those implied by the profile. Uses reverse domain notation.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `token[] profiles:capabilities` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    USDPROFILES_API
    UsdAttribute GetCapabilitiesAttr() const;

    /// See GetCapabilitiesAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDPROFILES_API
    UsdAttribute CreateCapabilitiesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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

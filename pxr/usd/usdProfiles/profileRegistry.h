//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDPROFILES_PROFILEREGISTRY_H
#define USDPROFILES_PROFILEREGISTRY_H

/// \file usdProfiles/profileRegistry.h

#include "pxr/pxr.h"
#include "pxr/usd/usdProfiles/api.h"

#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/token.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdProfilesProfileRegistry
///
/// Singleton registry for USD Profiles.
///
/// A Profile is a capability (declared in plugInfo.json with
/// \c "isProfile": true) that names a coherent set of functionality.
/// This registry provides profile-specific queries built on top of
/// \c UsdProfilesCapabilityRegistry.
///
/// \sa UsdProfilesCapabilityRegistry
///
class UsdProfilesProfileRegistry
{
public:
    /// Return the singleton instance.
    USDPROFILES_API
    static UsdProfilesProfileRegistry& GetInstance();

    /// Return true if \p id is a registered profile
    /// (a capability with \c "isProfile": true).
    USDPROFILES_API
    bool IsProfile(const TfToken& id) const;

    /// Return all registered profile ids.
    USDPROFILES_API
    TfTokenVector GetAllProfiles() const;

    /// Return the full set of capabilities that profile \p id satisfies,
    /// including itself and all transitive predecessors.
    /// Returns an empty vector if \p id is not a registered profile.
    USDPROFILES_API
    TfTokenVector GetProfileCapabilities(const TfToken& id) const;

    /// Return the docstring for profile \p id, or an empty string
    /// if \p id is not a registered profile.
    USDPROFILES_API
    std::string GetDocstring(const TfToken& id) const;

private:
    friend class TfSingleton<UsdProfilesProfileRegistry>;
    UsdProfilesProfileRegistry() = default;
};

USDPROFILES_API_TEMPLATE_CLASS(TfSingleton<UsdProfilesProfileRegistry>);

PXR_NAMESPACE_CLOSE_SCOPE

#endif

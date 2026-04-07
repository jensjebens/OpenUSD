//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDPROFILES_CAPABILITYREGISTRY_H
#define USDPROFILES_CAPABILITYREGISTRY_H

/// \file usdProfiles/capabilityRegistry.h

#include "pxr/pxr.h"
#include "pxr/usd/usdProfiles/api.h"

#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/token.h"

#include <map>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdProfilesCapabilityRegistry
///
/// Singleton registry of USD capability declarations.
///
/// Capabilities are declared in \c plugInfo.json files under a
/// \c "Capabilities" key in the \c "Info" section of a plugin:
///
/// \code{.json}
/// {
///   "Plugins": [{
///     "Info": {
///       "Capabilities": {
///         "com.example.myCapability": {
///           "docstring": "Description of the capability",
///           "predecessors": ["usd"],
///           "isProfile": false
///         }
///       }
///     }
///   }]
/// }
/// \endcode
///
/// Predecessors define the DAG edges: a capability implicitly satisfies all
/// of its (transitive) predecessors. A capability with \c "isProfile": true
/// is also accessible as a profile through \c UsdProfilesProfileRegistry.
///
/// The registry is populated lazily on first access and is read-only
/// thereafter (thread-safe for concurrent reads).
///
class UsdProfilesCapabilityRegistry
{
public:
    /// Return the singleton instance, loading capabilities from all
    /// registered plugins on first call.
    USDPROFILES_API
    static UsdProfilesCapabilityRegistry& GetInstance();

    /// Return true if \p id names a registered capability.
    USDPROFILES_API
    bool IsCapability(const TfToken& id) const;

    /// Return true if \p id names a registered capability that is also
    /// tagged as a profile (\c "isProfile": true in plugInfo.json).
    USDPROFILES_API
    bool IsProfile(const TfToken& id) const;

    /// Return the direct predecessor capability ids for \p id, or an
    /// empty vector if \p id is not registered.
    USDPROFILES_API
    TfTokenVector GetPredecessors(const TfToken& id) const;

    /// Return the transitive closure of predecessors for \p id (all
    /// ancestors in the capability DAG), not including \p id itself.
    /// Returns an empty vector if \p id is not registered.
    USDPROFILES_API
    TfTokenVector GetTransitivePredecessors(const TfToken& id) const;

    /// Return the docstring associated with \p id, or an empty string
    /// if \p id is not registered.
    USDPROFILES_API
    std::string GetDocstring(const TfToken& id) const;

    /// Return all registered capability ids.
    USDPROFILES_API
    TfTokenVector GetAllCapabilities() const;

    /// Return all registered capability ids that are also profiles.
    USDPROFILES_API
    TfTokenVector GetAllProfiles() const;

private:
    friend class TfSingleton<UsdProfilesCapabilityRegistry>;
    UsdProfilesCapabilityRegistry();

    void _LoadFromPlugins();

    struct _CapabilityInfo {
        TfToken      id;
        std::string  docstring;
        TfTokenVector predecessors;
        bool         isProfile = false;
    };

    std::map<TfToken, _CapabilityInfo> _capabilities;
};

USDPROFILES_API_TEMPLATE_CLASS(TfSingleton<UsdProfilesCapabilityRegistry>);

PXR_NAMESPACE_CLOSE_SCOPE

#endif

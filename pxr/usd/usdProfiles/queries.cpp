//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/queries.h"
#include "pxr/usd/usdProfiles/capabilityRegistry.h"
#include "pxr/usd/usdProfiles/profileAPI.h"
#include "pxr/usd/usdProfiles/profileRegistry.h"
#include "pxr/usd/usdProfiles/tokens.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"

#include <set>

PXR_NAMESPACE_OPEN_SCOPE

TfToken
UsdProfilesGetEffectiveProfile(const UsdPrim& prim)
{
    if (!prim) {
        return TfToken();
    }

    // Walk prim and ancestors, returning the first authored profile token.
    UsdPrim current = prim;
    while (current) {
        if (current.HasAPI<UsdProfilesProfileAPI>()) {
            UsdProfilesProfileAPI api(current);
            TfToken profile;
            if (api.GetProfileAttr().Get(&profile) && !profile.IsEmpty()) {
                return profile;
            }
        }
        current = current.GetParent();
    }

    return TfToken();
}

TfTokenVector
UsdProfilesGetEffectiveCapabilities(const UsdPrim& prim)
{
    if (!prim) {
        return {};
    }

    // Find the prim where the profile is authored.
    UsdPrim profilePrim;
    TfToken profile;

    UsdPrim current = prim;
    while (current) {
        if (current.HasAPI<UsdProfilesProfileAPI>()) {
            UsdProfilesProfileAPI api(current);
            TfToken tok;
            if (api.GetProfileAttr().Get(&tok) && !tok.IsEmpty()) {
                profilePrim = current;
                profile = tok;
                break;
            }
        }
        current = current.GetParent();
    }

    if (profile.IsEmpty()) {
        return {};
    }

    // Start with the profile's transitive capability set.
    UsdProfilesProfileRegistry& profReg =
        UsdProfilesProfileRegistry::GetInstance();
    TfTokenVector caps = profReg.GetProfileCapabilities(profile);

    // Add any explicit capabilities from profiles:capabilities.
    UsdProfilesProfileAPI api(profilePrim);
    VtArray<TfToken> explicitCaps;
    if (api.GetCapabilitiesAttr().Get(&explicitCaps)) {
        // Merge, avoiding duplicates.
        std::set<TfToken> seen(caps.begin(), caps.end());
        for (const TfToken& cap : explicitCaps) {
            if (seen.insert(cap).second) {
                caps.push_back(cap);
            }
            // Also include transitive predecessors of explicit capabilities.
            UsdProfilesCapabilityRegistry& capReg =
                UsdProfilesCapabilityRegistry::GetInstance();
            for (const TfToken& pred :
                     capReg.GetTransitivePredecessors(cap)) {
                if (seen.insert(pred).second) {
                    caps.push_back(pred);
                }
            }
        }
    }

    return caps;
}

PXR_NAMESPACE_CLOSE_SCOPE

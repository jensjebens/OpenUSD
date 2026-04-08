//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/profileRegistry.h"
#include "pxr/usd/usdProfiles/capabilityRegistry.h"

#include "pxr/base/tf/instantiateSingleton.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(UsdProfilesProfileRegistry);

/* static */
UsdProfilesProfileRegistry&
UsdProfilesProfileRegistry::GetInstance()
{
    return TfSingleton<UsdProfilesProfileRegistry>::GetInstance();
}

bool
UsdProfilesProfileRegistry::IsProfile(const TfToken& id) const
{
    return UsdProfilesCapabilityRegistry::GetInstance().IsProfile(id);
}

TfTokenVector
UsdProfilesProfileRegistry::GetAllProfiles() const
{
    return UsdProfilesCapabilityRegistry::GetInstance().GetAllProfiles();
}

TfTokenVector
UsdProfilesProfileRegistry::GetProfileCapabilities(const TfToken& id) const
{
    UsdProfilesCapabilityRegistry& capReg =
        UsdProfilesCapabilityRegistry::GetInstance();

    if (!capReg.IsProfile(id)) {
        return {};
    }

    // The profile itself plus all transitive predecessors.
    TfTokenVector result = capReg.GetTransitivePredecessors(id);
    result.insert(result.begin(), id);
    return result;
}

std::string
UsdProfilesProfileRegistry::GetDocstring(const TfToken& id) const
{
    if (!IsProfile(id)) {
        return {};
    }
    return UsdProfilesCapabilityRegistry::GetInstance().GetDocstring(id);
}

PXR_NAMESPACE_CLOSE_SCOPE

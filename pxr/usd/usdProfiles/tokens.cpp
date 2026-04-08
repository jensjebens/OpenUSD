//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdProfilesTokensType::UsdProfilesTokensType() :
    profilesCapabilities("profiles:capabilities", TfToken::Immortal),
    profilesProfile("profiles:profile", TfToken::Immortal),
    ProfileAPI("ProfileAPI", TfToken::Immortal),
    allTokens({
        profilesCapabilities,
        profilesProfile,
        ProfileAPI
    })
{
}

TfStaticData<UsdProfilesTokensType> UsdProfilesTokens;

PXR_NAMESPACE_CLOSE_SCOPE

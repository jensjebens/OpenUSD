//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDPROFILES_QUERIES_H
#define USDPROFILES_QUERIES_H

/// \file usdProfiles/queries.h
///
/// Convenience query functions for the USD Profiles system.

#include "pxr/pxr.h"
#include "pxr/usd/usdProfiles/api.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/base/tf/token.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Return the effective profile for \p prim.
///
/// Walks the prim and its ancestors looking for an applied ProfileAPI with a
/// non-empty \c profiles:profile attribute. Returns the first profile token
/// found, or an empty token if none is authored anywhere in the ancestor chain.
///
/// This implements the "nearest ancestor wins" resolution rule described in
/// the USD Profiles proposal.
///
/// \sa UsdProfilesProfileAPI
USDPROFILES_API
TfToken UsdProfilesGetEffectiveProfile(const UsdPrim& prim);

/// Return the effective capability list for \p prim.
///
/// Combines:
/// 1. The capabilities declared by the effective profile (and all transitive
///    predecessors of that profile).
/// 2. Any additional capabilities listed in \c profiles:capabilities on the
///    prim at which the profile was found.
///
/// Returns an empty vector if no ProfileAPI is found in the ancestor chain.
///
/// \sa UsdProfilesGetEffectiveProfile
/// \sa UsdProfilesCapabilityRegistry
USDPROFILES_API
TfTokenVector UsdProfilesGetEffectiveCapabilities(const UsdPrim& prim);

PXR_NAMESPACE_CLOSE_SCOPE

#endif

//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDMETRICS_TOKENS_H
#define USDMETRICS_TOKENS_H

/// \file usdMetricsApi/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class UsdMetricsTokensType
///
/// \link UsdMetricsTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdMetricsTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdMetricsTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdMetricsTokens->metricsKilogramsPerUnit);
/// \endcode
struct UsdMetricsTokensType {
    USDMETRICSAPI_API UsdMetricsTokensType();
    /// \brief "metrics:kilogramsPerUnit"
    /// 
    /// UsdMetricsPhysicsMetricsAPI
    const TfToken metricsKilogramsPerUnit;
    /// \brief "metrics:metersPerUnit"
    /// 
    /// UsdMetricsGeomMetricsAPI
    const TfToken metricsMetersPerUnit;
    /// \brief "metrics:upAxis"
    /// 
    /// UsdMetricsGeomMetricsAPI
    const TfToken metricsUpAxis;
    /// \brief "Y"
    /// 
    /// Fallback value for UsdMetricsGeomMetricsAPI::GetUpAxisAttr()
    const TfToken Y;
    /// \brief "Z"
    /// 
    /// Possible value for UsdMetricsGeomMetricsAPI::GetUpAxisAttr()
    const TfToken Z;
    /// \brief "GeomMetricsAPI"
    /// 
    /// Schema identifer and family for UsdMetricsGeomMetricsAPI
    const TfToken GeomMetricsAPI;
    /// \brief "PhysicsMetricsAPI"
    /// 
    /// Schema identifer and family for UsdMetricsPhysicsMetricsAPI
    const TfToken PhysicsMetricsAPI;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdMetricsTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdMetricsTokensType
extern USDMETRICSAPI_API TfStaticData<UsdMetricsTokensType> UsdMetricsTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif

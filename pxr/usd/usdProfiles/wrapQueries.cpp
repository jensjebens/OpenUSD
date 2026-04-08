//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/queries.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/base/tf/pyResultConversions.h"

#include "pxr/external/boost/python.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

void wrapUsdProfilesQueries()
{
    def("GetEffectiveProfile",
        UsdProfilesGetEffectiveProfile,
        (arg("prim")));

    def("GetEffectiveCapabilities",
        UsdProfilesGetEffectiveCapabilities,
        (arg("prim")),
        return_value_policy<TfPySequenceToList>());
}

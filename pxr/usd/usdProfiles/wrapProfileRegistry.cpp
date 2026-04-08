//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/profileRegistry.h"

#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"

#include "pxr/external/boost/python.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

void wrapUsdProfilesProfileRegistry()
{
    typedef UsdProfilesProfileRegistry This;

    class_<This, pxr_boost::python::noncopyable>("ProfileRegistry", no_init)
        .def("GetInstance",
             &This::GetInstance,
             return_value_policy<reference_existing_object>())
        .staticmethod("GetInstance")

        .def("IsProfile", &This::IsProfile, (arg("id")))

        .def("GetAllProfiles",
             &This::GetAllProfiles,
             return_value_policy<TfPySequenceToList>())

        .def("GetProfileCapabilities",
             &This::GetProfileCapabilities,
             (arg("id")),
             return_value_policy<TfPySequenceToList>())

        .def("GetDocstring", &This::GetDocstring, (arg("id")))
    ;
}

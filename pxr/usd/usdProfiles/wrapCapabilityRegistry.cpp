//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/capabilityRegistry.h"

#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"

#include "pxr/external/boost/python.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

void wrapUsdProfilesCapabilityRegistry()
{
    typedef UsdProfilesCapabilityRegistry This;

    class_<This, pxr_boost::python::noncopyable>("CapabilityRegistry", no_init)
        .def("GetInstance",
             &This::GetInstance,
             return_value_policy<reference_existing_object>())
        .staticmethod("GetInstance")

        .def("IsCapability", &This::IsCapability, (arg("id")))
        .def("IsProfile",    &This::IsProfile,    (arg("id")))

        .def("GetPredecessors",
             &This::GetPredecessors,
             (arg("id")),
             return_value_policy<TfPySequenceToList>())

        .def("GetTransitivePredecessors",
             &This::GetTransitivePredecessors,
             (arg("id")),
             return_value_policy<TfPySequenceToList>())

        .def("GetDocstring", &This::GetDocstring, (arg("id")))

        .def("GetAllCapabilities",
             &This::GetAllCapabilities,
             return_value_policy<TfPySequenceToList>())

        .def("GetAllProfiles",
             &This::GetAllProfiles,
             return_value_policy<TfPySequenceToList>())

        .def("GetValidators",
             &This::GetValidators,
             (arg("id")),
             return_value_policy<TfPySequenceToList>())

        .def("GetAllValidatorsForCapability",
             &This::GetAllValidatorsForCapability,
             (arg("id")),
             return_value_policy<TfPySequenceToList>())
    ;
}

//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// GENERATED FILE.  DO NOT EDIT.
#include "pxr/external/boost/python/class.hpp"
#include "./tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define _ADD_TOKEN(cls, name) \
    cls.add_static_property(#name, +[]() { return UsdMetricsTokens->name.GetString(); });

void wrapUsdMetricsTokens()
{
    pxr_boost::python::class_<UsdMetricsTokensType, pxr_boost::python::noncopyable>
        cls("Tokens", pxr_boost::python::no_init);
    _ADD_TOKEN(cls, inherited);
    _ADD_TOKEN(cls, metricsKilogramsPerUnit);
    _ADD_TOKEN(cls, metricsMetersPerUnit);
    _ADD_TOKEN(cls, metricsUpAxis);
    _ADD_TOKEN(cls, Y);
    _ADD_TOKEN(cls, Z);
    _ADD_TOKEN(cls, GeomMetricsAPI);
    _ADD_TOKEN(cls, PhysicsMetricsAPI);
}

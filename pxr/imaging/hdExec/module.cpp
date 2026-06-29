//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/external/boost/python/module.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

void wrapExecComputedTransformSceneIndex();

PXR_BOOST_PYTHON_MODULE(_hdExec)
{
    wrapExecComputedTransformSceneIndex();
}

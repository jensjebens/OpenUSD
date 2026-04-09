//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDMETRICSAPI_API_H
#define USDMETRICSAPI_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDMETRICSAPI_API
#   define USDMETRICSAPI_API_TEMPLATE_CLASS(...)
#   define USDMETRICSAPI_API_TEMPLATE_STRUCT(...)
#   define USDMETRICSAPI_LOCAL
#else
#   if defined(USDMETRICSAPI_EXPORTS)
#       define USDMETRICSAPI_API ARCH_EXPORT
#       define USDMETRICSAPI_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDMETRICSAPI_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDMETRICSAPI_API ARCH_IMPORT
#       define USDMETRICSAPI_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDMETRICSAPI_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDMETRICSAPI_LOCAL ARCH_HIDDEN
#endif

#endif

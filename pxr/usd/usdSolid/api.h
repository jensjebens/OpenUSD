//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_API_H
#define USDSOLID_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDSOLID_API
#   define USDSOLID_API_TEMPLATE_CLASS(...)
#   define USDSOLID_API_TEMPLATE_STRUCT(...)
#   define USDSOLID_LOCAL
#else
#   if defined(USDSOLID_EXPORTS)
#       define USDSOLID_API ARCH_EXPORT
#       define USDSOLID_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDSOLID_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDSOLID_API ARCH_IMPORT
#       define USDSOLID_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDSOLID_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDSOLID_LOCAL ARCH_HIDDEN
#endif

#endif

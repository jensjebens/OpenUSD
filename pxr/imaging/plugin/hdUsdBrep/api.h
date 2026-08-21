// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef PXR_IMAGING_PLUGIN_HD_SMLIB_API_H
#define PXR_IMAGING_PLUGIN_HD_SMLIB_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define HDSMLIB_API
#   define HDSMLIB_API_TEMPLATE_CLASS(...)
#   define HDSMLIB_API_TEMPLATE_STRUCT(...)
#   define HDSMLIB_LOCAL
#else
#   if defined(HDSMLIB_EXPORTS)
#       define HDSMLIB_API ARCH_EXPORT
#       define HDSMLIB_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDSMLIB_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define HDSMLIB_API ARCH_IMPORT
#       define HDSMLIB_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDSMLIB_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define HDSMLIB_LOCAL ARCH_HIDDEN
#endif

#endif // PXR_IMAGING_PLUGIN_HD_SMLIB_API_H

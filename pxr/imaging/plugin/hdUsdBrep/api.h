// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef PXR_IMAGING_PLUGIN_HD_USD_BREP_API_H
#define PXR_IMAGING_PLUGIN_HD_USD_BREP_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define HDUSDBREP_API
#   define HDUSDBREP_API_TEMPLATE_CLASS(...)
#   define HDUSDBREP_API_TEMPLATE_STRUCT(...)
#   define HDUSDBREP_LOCAL
#else
#   if defined(HDUSDBREP_EXPORTS)
#       define HDUSDBREP_API ARCH_EXPORT
#       define HDUSDBREP_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDUSDBREP_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define HDUSDBREP_API ARCH_IMPORT
#       define HDUSDBREP_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDUSDBREP_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define HDUSDBREP_LOCAL ARCH_HIDDEN
#endif

#endif // PXR_IMAGING_PLUGIN_HD_USD_BREP_API_H

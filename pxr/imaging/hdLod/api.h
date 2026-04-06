//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_LOD_API_H
#define PXR_IMAGING_HD_LOD_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define HDLOD_API
#   define HDLOD_API_TEMPLATE_CLASS(...)
#   define HDLOD_API_TEMPLATE_STRUCT(...)
#   define HDLOD_LOCAL
#else
#   if defined(HDLOD_EXPORTS)
#       define HDLOD_API ARCH_EXPORT
#       define HDLOD_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDLOD_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define HDLOD_API ARCH_IMPORT
#       define HDLOD_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDLOD_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define HDLOD_LOCAL ARCH_HIDDEN
#endif

#endif // PXR_IMAGING_HD_LOD_API_H

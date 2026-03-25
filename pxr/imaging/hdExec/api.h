//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_EXEC_API_H
#define PXR_IMAGING_HD_EXEC_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define HDEXEC_API
#   define HDEXEC_API_TEMPLATE_CLASS(...)
#   define HDEXEC_API_TEMPLATE_STRUCT(...)
#   define HDEXEC_LOCAL
#else
#   if defined(HDEXEC_EXPORTS)
#       define HDEXEC_API ARCH_EXPORT
#       define HDEXEC_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDEXEC_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define HDEXEC_API ARCH_IMPORT
#       define HDEXEC_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define HDEXEC_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define HDEXEC_LOCAL ARCH_HIDDEN
#endif

#endif // PXR_IMAGING_HD_EXEC_API_H

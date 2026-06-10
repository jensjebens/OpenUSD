// Copyright 2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
//
// hdOcct - API Export Macros
//
// pxr_plugin(hdOcct ...) defines HDOCCT_EXPORTS=1 when building this library.
// Consumers linking against hdOcct will not have that define set, so they
// get ARCH_IMPORT (dllimport on Windows, nothing on Linux/macOS).

#ifndef PXR_IMAGING_HDOCCT_API_H
#define PXR_IMAGING_HDOCCT_API_H

#include "pxr/base/arch/export.h"

#if defined(HDOCCT_EXPORTS)
#   define HDOCCT_API ARCH_EXPORT
#   define HDOCCT_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#   define HDOCCT_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#else
#   define HDOCCT_API ARCH_IMPORT
#   define HDOCCT_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#   define HDOCCT_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#endif

#define HDOCCT_LOCAL ARCH_HIDDEN

// Backward compatibility aliases (used throughout existing code)
#define USDSOLIDTESSELLATOR_API HDOCCT_API
#define USDSOLIDTESSELLATOR_API_TEMPLATE_CLASS HDOCCT_API_TEMPLATE_CLASS
#define USDSOLIDTESSELLATOR_API_TEMPLATE_STRUCT HDOCCT_API_TEMPLATE_STRUCT
#define USDSOLIDTESSELLATOR_LOCAL HDOCCT_LOCAL

#endif // PXR_IMAGING_HDOCCT_API_H

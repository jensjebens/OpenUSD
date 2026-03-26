#ifndef USD_METRICS_API_H
#define USD_METRICS_API_H

/// \file usdMetricsApi/api.h

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USD_METRICS_API
#   define USD_METRICS_API_TEMPLATE_CLASS(...)
#   define USD_METRICS_API_TEMPLATE_STRUCT(...)
#else
#   if defined(USD_METRICS_API_EXPORTS)
#       define USD_METRICS_API ARCH_EXPORT
#       define USD_METRICS_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USD_METRICS_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USD_METRICS_API ARCH_IMPORT
#       define USD_METRICS_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USD_METRICS_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#endif

#endif // USD_METRICS_API_H

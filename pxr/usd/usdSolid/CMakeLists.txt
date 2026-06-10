set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdSolid)

# UsdSolid schema: BrepArray solid modeling prim type.
# Inherits from UsdGeomGprim. Provides the schema definition for
# solid boundary representation models.
#
# Full C++ generated API classes will be added when usdGenSchema is
# run against the schema.usda. For now this installs the schema
# resources so the prim type is recognized by the USD runtime.

pxr_library(${PXR_PACKAGE}
    LIBRARIES
        tf
        sdf
        usd
        usdGeom

    CPPFILES
        module.cpp

    RESOURCE_FILES
        plugInfo.json
        generatedSchema.usda
        schema.usda

    DISABLE_PRECOMPILED_HEADERS
)

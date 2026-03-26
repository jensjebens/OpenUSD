set(PXR_PACKAGE usdMetricsApi)

pxr_plugin(${PXR_PACKAGE}
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        sdf
        usd
        usdGeom
        plug
        js

    INCLUDE_DIRS
        ${Boost_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_DIRS}

    PUBLIC_HEADERS
        api.h
        dimensionalRegistry.h
        geomMetricsAPI.h
        physicsMetricsAPI.h
        tokens.h

    CPPFILES
        dimensionalRegistry.cpp
        geomMetricsAPI.cpp
        physicsMetricsAPI.cpp
        tokens.cpp
        module.cpp

    PYTHON_CPPFILES
        wrapGeomMetricsAPI.cpp
        wrapPhysicsMetricsAPI.cpp
        wrapTokens.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
        schema.usda
        generatedSchema.usda
)

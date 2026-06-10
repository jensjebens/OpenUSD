set(PXR_PREFIX extras/usd)
set(PXR_PACKAGE usdSolidTessellator)

# usdsolidtessellate: Command-line tool to convert BrepArray prims to Mesh.
# Requires PXR_BUILD_OCCT_PLUGIN=ON (the hdOcct library provides tessellation).

if (NOT ${PXR_BUILD_OCCT_PLUGIN})
    message(STATUS
        "Skipping ${PXR_PACKAGE} because PXR_BUILD_OCCT_PLUGIN=OFF")
    return()
endif()

if(NOT OpenCASCADE_FOUND AND NOT OCCT_INCLUDE_DIR)
    message(WARNING "Not building ${PXR_PACKAGE}: OpenCASCADE not found")
    return()
endif()

# Determine OCCT paths
if (OpenCASCADE_FOUND AND DEFINED OpenCASCADE_INCLUDE_DIR)
    set(_OCCT_INCLUDE_DIR ${OpenCASCADE_INCLUDE_DIR})
    set(_OCCT_LIBRARIES ${OpenCASCADE_LIBRARIES})
else()
    set(_OCCT_INCLUDE_DIR ${OCCT_INCLUDE_DIR})
    set(_OCCT_LIBRARIES ${OCCT_LIBRARIES})
endif()

add_executable(usdsolidtessellate
    meshExport.cpp
)

target_include_directories(usdsolidtessellate PRIVATE
    ${PROJECT_SOURCE_DIR}/pxr/imaging/plugin/hdOcct
    ${_OCCT_INCLUDE_DIR}
)

target_link_libraries(usdsolidtessellate PRIVATE
    hdOcct
    tf
    gf
    vt
    sdf
    usd
    usdGeom
    ${_OCCT_LIBRARIES}
)

# If USD was built with Python support, the CLI needs to link against
# libpython to resolve VtValue template instantiations.
if (NOT WIN32)
    find_package(Python3 COMPONENTS Development QUIET)
    if (Python3_FOUND)
        target_link_libraries(usdsolidtessellate PRIVATE Python3::Python)
    endif()
endif()

install(TARGETS usdsolidtessellate
    RUNTIME DESTINATION bin
)

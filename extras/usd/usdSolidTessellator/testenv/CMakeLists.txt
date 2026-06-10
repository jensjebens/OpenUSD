if(BUILD_TESTING)
    # Basic tessellation round-trip test
    add_executable(testUsdSolidTessellator
        testTessellator.cpp
    )

    target_link_libraries(testUsdSolidTessellator
        PRIVATE
            usdSolidTessellator
            usd
            usdGeom
            tf
            sdf
    )

    add_test(
        NAME testUsdSolidTessellator
        COMMAND testUsdSolidTessellator
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Copy test data
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/testCube.usda
        ${CMAKE_CURRENT_BINARY_DIR}/testCube.usda
        COPYONLY
    )
endif()

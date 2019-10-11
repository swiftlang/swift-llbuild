set(LLBuild_EXPORTS_FILE ${CMAKE_CURRENT_BINARY_DIR}/LLBuildExports.cmake)
configure_file(LLBuildConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LLBuildConfig.cmake)

get_property(LLBuild_EXPORTS GLOBAL PROPERTY LLBuild_EXPORTS)
export(TARGETS ${LLBuild_EXPORTS} FILE ${LLBuild_EXPORTS_FILE})

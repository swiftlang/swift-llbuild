set(SOURCES 
  BuildSystem-C-API.cpp
  C-API.cpp
  Core-C-API.cpp)
set(DEPENDENCIES
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3
  curses)

add_llbuild_library(libllbuild
  ${SOURCES}
  SHARED
  OUTPUT_NAME llbuild)

set_property(TARGET libllbuild PROPERTY MACOSX_RPATH ON)

target_link_libraries(libllbuild ${DEPENDENCIES})

include_directories(BEFORE
  ${CMAKE_CURRENT_SOURCE_DIR}/public-api)

install(DIRECTORY public-api/
  DESTINATION include
  COMPONENT libllbuild
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.modulemap")

install(TARGETS libllbuild
        DESTINATION lib
        COMPONENT libllbuild)

add_custom_target(install-libllbuild
                  DEPENDS libllbuild
                  COMMENT "Installing libllbuild..."
                  COMMAND "${CMAKE_COMMAND}"
                          -DCMAKE_INSTALL_COMPONENT=libllbuild
                          -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # On OS X, add a target to create the llbuild.framework product.
  add_library(llbuild-framework SHARED ${SOURCES})
  set_target_properties(llbuild-framework PROPERTIES FRAMEWORK 1)
  set_target_properties(llbuild-framework PROPERTIES MACOSX_RPATH "@rpath")
  set_target_properties(llbuild-framework PROPERTIES OUTPUT_NAME llbuild)
  set_target_properties(llbuild-framework PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${LLBUILD_LIBRARY_OUTPUT_INTDIR})
  target_link_libraries(llbuild-framework ${DEPENDENCIES})

  # Manually copy the API headers.
  file(COPY DIRECTORY public-api/llbuild/
    DESTINATION ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Headers
    FILES_MATCHING
    PATTERN "*.h")

  # Manually copy the framework module map.
  add_custom_target(llbuild-framework-module-map COMMAND ${CMAKE_COMMAND} -E make_directory ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Modules/ COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../llbuild-framework/llbuild-module.modulemap ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Modules/module.modulemap)

  # Create the appropriate symlinks.
  add_custom_target(llbuild-framework-headers-symlink COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Headers ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Headers)
  add_custom_target(llbuild-framework-modules-symlink COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Modules ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Modules)
  add_dependencies(llbuild-framework llbuild-framework-headers-symlink llbuild-framework-modules-symlink llbuild-framework-module-map)
endif()

set(SOURCES 
  BuildSystem-C-API.cpp
  C-API.cpp
  Core-C-API.cpp
  BuildDB-C-API.cpp
  BuildKey-C-API.cpp
  BuildValue-C-API.cpp)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
set(LIBLLBUILD_NAME libllbuild)
else()
set(LIBLLBUILD_NAME llbuild)
endif()

add_llbuild_library(libllbuild
  ${SOURCES}
  SHARED
  OUTPUT_NAME ${LIBLLBUILD_NAME})

set_property(TARGET libllbuild PROPERTY MACOSX_RPATH ON)

target_link_libraries(libllbuild PRIVATE
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)
if(NOT CMAKE_SYSTEM_NAME STREQUAL Windows)
  target_link_libraries(libllbuild PRIVATE
    curses)
endif()

include_directories(BEFORE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

install(DIRECTORY include/
  DESTINATION include
  COMPONENT libllbuild
  FILES_MATCHING
  PATTERN "*.h")

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/
  DESTINATION include
  COMPONENT libllbuild
  FILES_MATCHING
  PATTERN "*.h")

install(TARGETS libllbuild
  ARCHIVE DESTINATION lib${LLBUILD_LIBDIR_SUFFIX}
  LIBRARY DESTINATION lib${LLBUILD_LIBDIR_SUFFIX}
  RUNTIME DESTINATION bin
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
  set_target_properties(llbuild-framework PROPERTIES LINK_FLAGS " -compatibility_version 1 -current_version 1")
  target_link_libraries(llbuild-framework PRIVATE
    llbuildBuildSystem
    llbuildCore
    llbuildBasic
    llvmSupport
    SQLite::SQLite3
    curses)

  # Manually set up the remaining framework structure.
  set(LLBUILD_FW_INPUTS)

  # Copy the public headers.
  file(GLOB headers include/llbuild/*.h)
  foreach(header ${headers})
    get_filename_component(name ${header} NAME)
    set(dst ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Headers/${name})
    list(APPEND LLBUILD_FW_INPUTS ${dst})
    add_custom_command(OUTPUT ${dst} COMMAND ${CMAKE_COMMAND} -E make_directory ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Headers/ COMMAND ${CMAKE_COMMAND} -E copy ${header} ${dst} DEPENDS ${header})
  endforeach()

  # Copy the framework module map.
  set(src ${CMAKE_CURRENT_SOURCE_DIR}/../llbuild-framework/llbuild-module.modulemap)
  set(dst ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Modules/module.modulemap)
  list(APPEND LLBUILD_FW_INPUTS ${dst})
  add_custom_command(OUTPUT ${dst} COMMAND ${CMAKE_COMMAND} -E make_directory ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Versions/A/Modules/ COMMAND ${CMAKE_COMMAND} -E copy ${src} ${dst} DEPENDS ${src})

  # Create the appropriate symlinks.
  set(dst ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Headers)
  list(APPEND LLBUILD_FW_INPUTS ${dst})
  add_custom_command(OUTPUT ${dst} COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Headers ${dst})
  set(dst ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/llbuild.framework/Modules)
  list(APPEND LLBUILD_FW_INPUTS ${dst})
  add_custom_command(OUTPUT ${dst} COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Modules ${dst})

  # Add a custom target for the setup tasks.
  add_custom_target(llbuild-framework-setup DEPENDS ${LLBUILD_FW_INPUTS})
  add_dependencies(llbuild-framework llbuild-framework-setup)
endif()

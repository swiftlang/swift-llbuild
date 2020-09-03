
if(CMAKE_VERSION VERSION_LESS 3.16)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL Windows AND NOT CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(CMAKE_SHARED_LIBRARY_SONAME_Swift_FLAG "-Xlinker -soname -Xlinker ")
  endif()

  if(NOT CMAKE_SYSTEM_NAME STREQUAL Windows)
    set(CMAKE_SHARED_LIBRARY_RUNTIME_Swift_FLAG "-Xlinker -rpath -Xlinker ")
    set(CMAKE_SHARED_LIBRARY_RUNTIME_Swift_FLAG_SEP ":")
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL Windows)
    set(CMAKE_LINK_LIBRARY_FLAG "-l")
    set(CMAKE_LINK_LIBRARY_SUFFIX "")
  endif()
endif()

# TODO(compnerd) move both of these outside of the CMake into the invocation
if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
  add_compile_options(-target ${CMAKE_OSX_ARCHITECTURES}-apple-macosx10.10)
  if(NOT CMAKE_OSX_SYSROOT STREQUAL "")
    add_compile_options(-sdk ${CMAKE_OSX_SYSROOT})
  endif()
endif()

add_library(llbuildSwift
  BuildSystemBindings.swift
  CoreBindings.swift
  BuildDBBindings.swift
  BuildKey.swift
  Internals.swift
  BuildValue.swift)
if(CMAKE_SYSTEM_NAME STREQUAL Windows)
  target_compile_options(llbuildSwift PRIVATE
    "SHELL:-Xcc -D_CRT_NONSTDC_NO_DEPRECATE")
endif()
target_link_libraries(llbuildSwift PRIVATE
    libllbuild)
if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
  set_target_properties(llbuildSwift PROPERTIES
    # RUNPATH for finding Swift core libraries in the toolchain.
    # FIXME: ideally, the second path should be passed from the swift-ci invocation
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path:@loader_path/../../macosx"
    MACOSX_RPATH TRUE)
  if(CMAKE_VERSION VERSION_LESS 3.16)
    target_link_options(llbuildSwift PRIVATE
      "SHELL:-Xlinker -install_name -Xlinker @rpath/$<TARGET_FILE_NAME:llbuildSwift>")
  else()
    set_target_properties(llbuildSwift PROPERTIES
      BUILD_WITH_INSTALL_NAME_DIR TRUE
      INSTALL_NAME_DIR "@rpath")
  endif()
else()
  target_link_libraries(llbuildSwift PRIVATE
    SQLite::SQLite3)
  target_link_libraries(llbuildSwift PRIVATE
    swiftDispatch
    Foundation)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL Windows)
    target_link_options(llbuildSwift PRIVATE "SHELL:-no-toolchain-stdlib-rpath")
    set_target_properties(llbuildSwift PROPERTIES
      INSTALL_RPATH "$ORIGIN:$ORIGIN/../../$<LOWER_CASE:${CMAKE_SYSTEM_NAME}>")
  endif()
endif()
set_target_properties(llbuildSwift PROPERTIES
  Swift_MODULE_NAME llbuildSwift
  INTERFACE_LINK_LIBRARIES libllbuild
  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS llbuildSwift
  ARCHIVE DESTINATION lib/swift/pm/llbuild COMPONENT libllbuildSwift
  LIBRARY DESTINATION lib/swift/pm/llbuild COMPONENT libllbuildSwift
  RUNTIME DESTINATION bin COMPONENT libllbuildSwift)
set_property(GLOBAL APPEND PROPERTY LLBuild_EXPORTS llbuildSwift)

# Add install target.
add_custom_target(install-libllbuildSwift
  DEPENDS llbuildSwift
  COMMENT "Installing libllbuildSwift..."
  COMMAND "${CMAKE_COMMAND}"
    -DCMAKE_INSTALL_COMPONENT=libllbuildSwift
    -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")

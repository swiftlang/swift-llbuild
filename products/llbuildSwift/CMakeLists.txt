# Returns the current architecture name in a variable
#
# Usage:
#   get_swift_host_arch(result_var_name)
#
# If the current architecture is supported by Swift, sets ${result_var_name}
# with the sanitized host architecture name derived from CMAKE_SYSTEM_PROCESSOR.
function(get_swift_host_arch result_var_name)
  if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "amd64|x86_64|s390x|wasm32")
    set("${result_var_name}" "${CMAKE_SYSTEM_PROCESSOR}" PARENT_SCOPE)
  elseif ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "AArch64|aarch64|arm64")
    if(CMAKE_SYSTEM_NAME MATCHES Darwin)
      set("${result_var_name}" "arm64" PARENT_SCOPE)
    else()
      set("${result_var_name}" "aarch64" PARENT_SCOPE)
    endif()
  elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "ppc64")
    set("${result_var_name}" "powerpc64" PARENT_SCOPE)
  elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "ppc64le")
    set("${result_var_name}" "powerpc64le" PARENT_SCOPE)
  elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "armv6l")
    set("${result_var_name}" "armv6" PARENT_SCOPE)
  elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "armv7-a|armv7l")
    set("${result_var_name}" "armv7" PARENT_SCOPE)
  elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "AMD64")
    set("${result_var_name}" "x86_64" PARENT_SCOPE)
  elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "IA64")
    set("${result_var_name}" "itanium" PARENT_SCOPE)
  elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "x86|i686")
    set("${result_var_name}" "i686" PARENT_SCOPE)
  else()
    message(FATAL_ERROR "Unrecognized architecture on host system: ${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endfunction()

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

# Set x86_64 default if CMAKE_OSX_ARCHITECTURES is not set
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
  set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
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
    get_swift_host_arch(swift_arch)
    set_target_properties(llbuildSwift PROPERTIES
      INSTALL_RPATH "$ORIGIN/../../$<LOWER_CASE:${CMAKE_SYSTEM_NAME}>/${swift_arch}")
  endif()
endif()
set_target_properties(llbuildSwift PROPERTIES
  Swift_MODULE_NAME llbuildSwift
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR};${PROJECT_SOURCE_DIR}/products/libllbuild/include")

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

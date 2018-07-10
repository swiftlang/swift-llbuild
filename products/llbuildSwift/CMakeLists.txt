# Set sources.
set(SOURCES 
  BuildSystemBindings.swift
  CoreBindings.swift)

# Load version number from the XCConfig
file(STRINGS "${CMAKE_SOURCE_DIR}/Xcode/Configs/Version.xcconfig" VERSION_XCCONFIG)
if(VERSION_XCCONFIG MATCHES ".*LLBUILD_C_API_VERSION = ([0-9]+).*")
  string(REGEX REPLACE ".*LLBUILD_C_API_VERSION = ([0-9]+).*" "\\1" LLBUILD_C_API_VERSION "${VERSION_XCCONFIG}")
else(VERSION_XCCONFIG MATCHES ".*LLBUILD_C_API_VERSION = ([0-9]+).*")
  message(FATAL_ERROR "Unable to find LLBUILD_C_API_VERSION in Version.xcconfig")
endif(VERSION_XCCONFIG MATCHES ".*LLBUILD_C_API_VERSION = ([0-9]+).*")


# Link C API.
list(APPEND additional_args -DLLBUILD_C_API_VERSION_${LLBUILD_C_API_VERSION} -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include -lllbuild)
 
if(APPLE)
  list(APPEND additional_args -target x86_64-apple-macosx10.10)
else()
  # On Linux, use Foundation and Dispatch libraries built and provided by swift's build-script.
  if(FOUNDATION_BUILD_DIR)
    list(APPEND additional_args
      -L${FOUNDATION_BUILD_DIR}/Foundation
      -I${FOUNDATION_BUILD_DIR}/Foundation
      -I${FOUNDATION_BUILD_DIR}/Foundation/usr/lib/swift)
  endif()
  if(LIBDISPATCH_BUILD_DIR)
    list(APPEND additional_args -L${LIBDISPATCH_BUILD_DIR}/src/.libs)
  endif()
  if(LIBDISPATCH_SOURCE_DIR)
    list(APPEND additional_args -I${LIBDISPATCH_SOURCE_DIR})
  endif()
endif()

# Add swift bindings target if swift compiler is present.
if (SWIFTC_FOUND)
  add_swift_module(libllbuildSwift llbuildSwift libllbuild "${SOURCES}" "${additional_args}")

  # Install the library.
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(DYLIB_EXT dylib)
  else()
    set(DYLIB_EXT so)
  endif()
  
  install(FILES "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuildSwift.${DYLIB_EXT}"
    DESTINATION lib/swift/pm/llbuild
    COMPONENT libllbuildSwift)
  
  # Add install target.
  add_custom_target(install-libllbuildSwift
    DEPENDS libllbuildSwift
    COMMENT "Installing libllbuildSwift..."
    COMMAND "${CMAKE_COMMAND}"
      -DCMAKE_INSTALL_COMPONENT=libllbuildSwift
      -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
  
endif()

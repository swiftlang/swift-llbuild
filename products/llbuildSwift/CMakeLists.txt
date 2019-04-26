# Set sources.
set(SOURCES 
  BuildSystemBindings.swift
  CoreBindings.swift)

# Link C API.
list(APPEND additional_args -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include -lllbuild)
 
if(APPLE)
  list(APPEND additional_args -target x86_64-apple-macosx10.10)
else()
  # On Linux, use Foundation and Dispatch libraries built and provided by swift's build-script.
  if(FOUNDATION_BUILD_DIR)
    list(APPEND additional_args
      -L${FOUNDATION_BUILD_DIR}
      -Fsystem ${FOUNDATION_BUILD_DIR}
      -Fsystem ${FOUNDATION_BUILD_DIR}/CoreFoundation-prefix/System/Library/Frameworks
      -I${FOUNDATION_BUILD_DIR}/swift)
  endif()
  if(LIBDISPATCH_BUILD_DIR)
    list(APPEND additional_args -L${LIBDISPATCH_BUILD_DIR})
    list(APPEND additional_args -L${LIBDISPATCH_BUILD_DIR}/src)
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

  install(TARGETS ${LIBLLBUILD_NAME}
          ARCHIVE DESTINATION lib${LLBUILD_LIBDIR_SUFFIX}/swift/pm/llbuild
          LIBRARY DESTINATION lib${LLBUILD_LIBDIR_SUFFIX}/swift/pm/llbuild
          RUNTIME DESTINATION bin
          COMPONENT ${LIBLLBUILD_NAME}Swift)

  include(SwiftSupport)

  string(TOLOWER ${CMAKE_SYSTEM_NAME} swift_os)
  get_swift_host_arch(swift_arch)


  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/llbuildSwift.swiftdoc
    ${CMAKE_CURRENT_BINARY_DIR}/llbuildSwift.swiftmodule
    DESTINATION
    lib/swift/${swift_os}/${swift_arch})

  if(BUILD_SHARED_LIBS)
    set(library_kind SHARED)
    set(swift_dir swift)
  else()
    set(library_kind STATIC)
    set(swift_dir swift_static)
  endif()

  set(llbuildSwift_OUTPUT_FILE
    ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/${CMAKE_${library_kind}_LIBRARY_PREFIX}llbuildSwift${CMAKE_${library_kind}_LIBRARY_SUFFIX})

  if(CMAKE_SYSTEM_NAME STREQUAL Windows AND BUILD_SHARED_LIBS)
    install(FILES
              ${llbuildSwift_OUTPUT_FILE}
            DESTINATION
              bin)
    install(FILES
              ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}llbuildSwift${CMAKE_IMPORT_LIBRARY_SUFFIX}
            DESTINATION
              lib/${swift_dir}/${swift_os})
  else()
    install(FILES
              ${llbuildSwift_OUTPUT_FILE}
            DESTINATION
              lib/${swift_dir}/${swift_os})
  endif()

  # Add install target.
  add_custom_target(install-libllbuildSwift
    DEPENDS libllbuildSwift
    COMMENT "Installing libllbuildSwift..."
    COMMAND "${CMAKE_COMMAND}"
      -DCMAKE_INSTALL_COMPONENT=libllbuildSwift
      -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
  
endif()

# Set sources.
set(SOURCES 
  BuildSystemBindings.swift
  CoreBindings.swift
  BuildDBBindings.swift
  BuildKey.swift
  Internals.swift
  BuildValue.swift)

# Link C API.
list(APPEND additional_args
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include
  -lllbuild)
 
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
    list(APPEND additional_args -L${LIBDISPATCH_BUILD_DIR}/bin)
    list(APPEND additional_args -L${LIBDISPATCH_BUILD_DIR}/src)
    list(APPEND additional_args -L${LIBDISPATCH_BUILD_DIR}/src/BlocksRuntime)
  endif()
  if(LIBDISPATCH_SOURCE_DIR)
    list(APPEND additional_args -I${LIBDISPATCH_SOURCE_DIR})
  endif()
  if(SWIFT_SDK)
    list(APPEND additional_args -sdk ${SWIFT_SDK})
  endif()
  if(SQLite3_FOUND)
    foreach(directory ${SQLite3_INCLUDE_DIRS})
      list(APPEND additional_args -I${directory})
    endforeach()
    foreach(library ${SQLite3_LIBRARIES})
      list(APPEND additional_args -l${library})
    endforeach()
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
      list(APPEND additional_args -Xcc;-D_DLL;-Xlinker;/NODEFAULTLIB:libcmt)
      list(APPEND additional_args -Xcc;-D_CRT_NONSTDC_NO_DEPRECATE)
    endif()
  endif()
endif()

# Add swift bindings target if swift compiler is present.
if(SWIFTC_FOUND)
  add_swift_module(libllbuildSwift llbuildSwift libllbuild "${SOURCES}" "${additional_args}")

  if(CMAKE_SYSTEM_NAME STREQUAL Windows)
    install(FILES ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_SHARED_LIBRARY_PREFIX}llbuildSwift${CMAKE_SHARED_LIBRARY_SUFFIX}
      DESTINATION bin
      COMPONENT libllbuildSwift)

    install(FILES ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_IMPORT_LIBRARY_PREFIX}llbuildSwift${CMAKE_IMPORT_LIBRARY_SUFFIX}
      DESTINATION lib
      COMPONENT libllbuildSwift)
  else()
    install(FILES ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${CMAKE_SHARED_LIBRARY_PREFIX}llbuildSwift${CMAKE_SHARED_LIBRARY_SUFFIX}
      DESTINATION lib/swift/pm/llbuild
      COMPONENT libllbuildSwift)
  endif()

  # Add install target.
  add_custom_target(install-libllbuildSwift
    DEPENDS libllbuildSwift
    COMMENT "Installing libllbuildSwift..."
    COMMAND "${CMAKE_COMMAND}"
      -DCMAKE_INSTALL_COMPONENT=libllbuildSwift
      -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
endif()

# Set sources.
set(SOURCES 
  BuildSystemBindings.swift
  CoreBindings.swift
  BuildDBBindings.swift
  BuildKey.swift
  Internals.swift
  BuildValue.swift)

# Link C API.
if(CMAKE_SYSTEM_NAME STREQUAL Windows)
  list(APPEND additional_args
    -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include
    -llibllbuild)
else()
  list(APPEND additional_args
    -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include
    -lllbuild)
endif()
 
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
  if(SQLite3_FOUND)
    foreach(directory ${SQLite3_INCLUDE_DIRS})
      list(APPEND additional_args -I${directory})
    endforeach()
    foreach(library ${SQLite3_LIBRARIES})
      list(APPEND additional_args -l${library})
    endforeach()
  endif()
endif()

# Add swift bindings target if swift compiler is present.
if (SWIFTC_FOUND)
  add_swift_module(libllbuildSwift llbuildSwift libllbuild "${SOURCES}" "${additional_args}")

  # Install the library.
  # Install both libllbuild and libllbuildSwift.
  if(CMAKE_SYSTEM_NAME STREQUAL Windows)
    list(APPEND LLBUILD_LIBRARIES
      "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}llbuild${CMAKE_IMPORT_LIBRARY_SUFFIX}"
      "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuildSwift${CMAKE_IMPORT_LIBRARY_SUFFIX}")
  else()
    list(APPEND LLBUILD_LIBRARIES
      "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuild${CMAKE_SHARED_LIBRARY_SUFFIX}"
      "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuildSwift${CMAKE_SHARED_LIBRARY_SUFFIX}")
  endif()
  
  if(CMAKE_SYSTEM_NAME STREQUAL Windows)
    install(FILES ${LLBUILD_EXECUTABLE_OUTPUT_INTDIR}/libllbuild${CMAKE_SHARED_LIBRARY_SUFFIX}
      ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuildSwift${CMAKE_SHARED_LIBRARY_SUFFIX}
      DESTINATION bin
      COMPONENT libllbuildSwift)
  endif()
  install(FILES ${LLBUILD_LIBRARIES}
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

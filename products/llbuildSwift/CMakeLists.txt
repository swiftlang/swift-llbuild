# Set sources.
set(SOURCES 
  BuildSystemBindings.swift
  CoreBindings.swift)

# Link C API.
if(CMAKE_SYSTEM_NAME STREQUAL Windows)
  list(APPEND additional_args -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include -llibllbuild)
else()
  list(APPEND additional_args -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include -lllbuild)
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
  if(CMAKE_SYSTEM_NAME STREQUAL Linux)
    list(APPEND additional_args -Xcc;-DDEPLOYMENT_TARGET_LINUX)
  elseif(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    list(APPEND additional_args -Xcc;-DDEPLOYMENT_TARGET_MACOSX)
  elseif(CMAKE_SYSTEM_NAME STREQUAL Windows)
    list(APPEND additional_args -Xcc;-DDEPLOYMENT_TARGET_WINDOWS)
  else()
    message(SEND_ERROR "unknown deployment target: ${CMAKE_SYSTEM_NAME}")
  endif()
  if(LLBUILD_PATH_TO_SQLITE_SOURCE)
    list(APPEND additional_args -I${LLBUILD_PATH_TO_SQLITE_SOURCE})
  endif()
  if(LLBUILD_PATH_TO_SQLITE_BUILD)
    list(APPEND additional_args -L${LLBUILD_PATH_TO_SQLITE_BUILD})
  endif()
endif()

# Add swift bindings target if swift compiler is present.
if (SWIFTC_FOUND)
  add_swift_module(libllbuildSwift llbuildSwift libllbuild "${SOURCES}" "${additional_args}")

  # Install both libllbuild and libllbuildSwift.
  list(APPEND LLBUILD_LIBRARIES "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuild${CMAKE_SHARED_LIBRARY_SUFFIX}")
  list(APPEND LLBUILD_LIBRARIES "${LLBUILD_LIBRARY_OUTPUT_INTDIR}/libllbuildSwift${CMAKE_SHARED_LIBRARY_SUFFIX}")
  
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

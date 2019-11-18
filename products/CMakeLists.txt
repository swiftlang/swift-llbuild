# Command line tools.
add_subdirectory(llbuild)
add_subdirectory(swift-build-tool)

# Public API products.
add_subdirectory(libllbuild)
if(Swift IN_LIST LLBUILD_SUPPORT_BINDINGS)
  enable_language(Swift)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL Darwin)
    find_package(Foundation CONFIG REQUIRED)
    find_package(dispatch CONFIG REQUIRED)
  endif()

  add_subdirectory(llbuildSwift)
endif()

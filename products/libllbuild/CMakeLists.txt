add_llbuild_library(libllbuild
  BuildSystem-C-API.cpp
  C-API.cpp
  Core-C-API.cpp
  SHARED
  OUTPUT_NAME llbuild)

set_property(TARGET libllbuild PROPERTY MACOSX_RPATH ON)

target_link_libraries(libllbuild
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3
  curses
  )

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

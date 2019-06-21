
include_directories(BEFORE
  ${CMAKE_SOURCE_DIR}/products/libllbuild/include)

add_llbuild_unittest(CAPITests
  C-API.cpp
  BuildSystem-C-API.cpp
  )

target_link_libraries(CAPITests PRIVATE
  llvmSupport
  llbuildBasic
  libllbuild
  SQLite::SQLite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(CAPITests PRIVATE
    curses)
endif()

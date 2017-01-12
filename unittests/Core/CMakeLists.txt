add_llbuild_unittest(CoreTests
  BuildEngineTest.cpp
  DependencyInfoParserTest.cpp
  DepsBuildEngineTest.cpp
  MakefileDepsParserTest.cpp
  )

target_link_libraries(CoreTests llbuildCore llbuildBasic llvmSupport sqlite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(CoreTests curses)
endif()


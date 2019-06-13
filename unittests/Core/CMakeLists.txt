add_llbuild_unittest(CoreTests
  BuildEngineTest.cpp
  BuildEngineCancellationTest.cpp
  DependencyInfoParserTest.cpp
  DepsBuildEngineTest.cpp
  MakefileDepsParserTest.cpp
  SQLiteBuildDBTest.cpp
  )

target_link_libraries(CoreTests
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(CoreTests curses)
endif()


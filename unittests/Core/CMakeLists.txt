add_llbuild_unittest(CoreTests
  BuildEngineTest.cpp
  BuildEngineCancellationTest.cpp
  DependencyInfoParserTest.cpp
  DepsBuildEngineTest.cpp
  MakefileDepsParserTest.cpp
  SQLiteBuildDBTest.cpp
  )

target_link_libraries(CoreTests PRIVATE
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(CoreTests PRIVATE
    curses)
endif()


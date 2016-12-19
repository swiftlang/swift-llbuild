add_llbuild_unittest(BuildSystemTests
  LaneBasedExecutionQueueTest
  BuildSystemFrontendTest.cpp
  TempDir.cpp
  )

target_link_libraries(BuildSystemTests
  llbuildBuildSystem
  llvmSupport
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3
  )

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(BuildSystemTests curses)
endif()

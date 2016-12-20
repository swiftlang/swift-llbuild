add_llbuild_unittest(BuildSystemTests
  LaneBasedExecutionQueueTest
  BuildSystemFrontendTest.cpp
  )

target_link_libraries(BuildSystemTests
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3
  curses
  )

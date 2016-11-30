add_llbuild_unittest(BuildSystemTests
  BuildExecutionQueueTest.cpp
  )
set(LLBUILD_BUILDSYSTEM_UNITTEST_PATH ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(BuildSystemTests llbuildBuildSystem llbuildBasic llbuildCore llvmSupport sqlite3 curses)

add_llbuild_unittest(BuildSystemTests
  LaneBasedExecutionQueueTest
  )

target_link_libraries(BuildSystemTests curses llbuildBuildSystem llvmSupport)

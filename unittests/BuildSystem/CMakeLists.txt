add_llbuild_unittest(BuildSystemTests
  LaneBasedExecutionQueueTest
  )

target_link_libraries(BuildSystemTests llbuildBuildSystem llvmSupport)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(BuildSystemTests curses)
endif()

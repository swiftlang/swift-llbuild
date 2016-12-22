add_llbuild_unittest(BasicTests
  FileSystemTest.cpp
  SerialQueueTest.cpp
  ShellUtilityTest.cpp
  )

target_link_libraries(BasicTests llbuildBasic llvmSupport)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(BasicTests curses)
endif()

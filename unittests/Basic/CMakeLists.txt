add_llbuild_unittest(BasicTests
  BinaryCodingTests.cpp
  FileSystemTest.cpp
  SerialQueueTest.cpp
  ShellUtilityTest.cpp
  ../BuildSystem/TempDir.cpp
  )

target_link_libraries(BasicTests llbuildBasic llvmSupport)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(BasicTests curses)
endif()

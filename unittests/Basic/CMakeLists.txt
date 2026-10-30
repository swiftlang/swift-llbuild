add_llbuild_unittest(BasicTests
  BinaryCodingTests.cpp
  Defer.cpp
  FileSystemTest.cpp
  POSIXEnvironmentTest.cpp
  SerialQueueTest.cpp
  ShellUtilityTest.cpp
  ../UnitTestSupport/TempDir.cpp
  )

target_include_directories(BasicTests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../UnitTestSupport/include)

target_link_libraries(BasicTests PRIVATE
  llbuildBasic
  llvmSupport)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(BasicTests PRIVATE
    curses)
endif()

add_llbuild_unittest(BasicTests
  FileSystemTest.cpp
  SerialQueueTest.cpp
  )

target_link_libraries(BasicTests llbuildBasic llvmSupport curses)

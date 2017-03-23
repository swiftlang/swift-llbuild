add_llbuild_unittest(CommandsTests
  InterruptSignalAwaiterTests.cpp
  )

target_link_libraries(CommandsTests llbuildCommands llbuildCore llbuildBasic llvmSupport sqlite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(CommandsTests curses)
endif()

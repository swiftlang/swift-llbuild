add_llbuild_unittest(EvoTests
  EvoEngineTest.cpp
  )

target_link_libraries(EvoTests PRIVATE
  llbuildEvo
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(EvoTests PRIVATE
    curses)
endif()


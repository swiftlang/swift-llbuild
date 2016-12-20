add_llbuild_unittest(NinjaTests
  LexerTest.cpp
  )

target_link_libraries(NinjaTests llbuildNinja)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(NinjaTests curses)
endif()

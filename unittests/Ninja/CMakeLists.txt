add_llbuild_unittest(NinjaTests
  LexerTest.cpp
  ManifestTest.cpp
  )

target_link_libraries(NinjaTests PRIVATE
  llbuildNinja)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(NinjaTests PRIVATE
    curses)
endif()

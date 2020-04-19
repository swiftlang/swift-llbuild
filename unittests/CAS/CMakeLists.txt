add_llbuild_unittest(CASTests
  DataIDTests.cpp
  )

target_link_libraries(CASTests PRIVATE
  llbuildCAS
  )

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(CASTests PRIVATE
    curses)
endif()


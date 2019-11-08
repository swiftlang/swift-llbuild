########################################################################
# CMake build script for time adjustment utility used in lit tests.
########################################################################

add_executable(adjust-times adjust-times.cpp)
target_link_libraries(adjust-times PRIVATE llvmSupport)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(adjust-times PRIVATE
    curses)
endif()

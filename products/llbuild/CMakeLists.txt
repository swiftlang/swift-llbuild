add_executable(llbuild
  main.cpp)

target_link_libraries(llbuild PRIVATE
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

if(CMAKE_SYSTEM_NAME STREQUAL Windows)
    set_target_properties(llbuild PROPERTIES LINK_FLAGS "/INCREMENTAL:NO")
endif()

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(llbuild PRIVATE
    curses)
endif()

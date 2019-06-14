add_llbuild_executable(llbuild
  main.cpp)

target_link_libraries(llbuild PRIVATE
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(llbuild PRIVATE
    curses)
endif()

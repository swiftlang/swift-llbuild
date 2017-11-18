add_llbuild_executable(llbuild
  main.cpp)

target_link_libraries(llbuild
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(llbuild curses)
endif()

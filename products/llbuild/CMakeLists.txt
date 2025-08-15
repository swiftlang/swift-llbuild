add_executable(llbuild-tool
  main.cpp)

target_link_libraries(llbuild-tool PRIVATE
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

set_target_properties(llbuild-tool PROPERTIES
  OUTPUT_NAME llbuild)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(llbuild-tool PRIVATE
    curses)
endif()

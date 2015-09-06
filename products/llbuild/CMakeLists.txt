add_llbuild_executable(llbuild
  llbuild.cpp)

target_link_libraries(llbuild
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3
  curses
  )

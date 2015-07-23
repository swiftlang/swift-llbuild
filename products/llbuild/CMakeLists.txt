add_llbuild_executable(llbuild
  llbuild.cpp)

target_link_libraries(llbuild
  llvmSupport
  llbuildBasic
  llbuildCore
  llbuildBuildSystem
  llbuildNinja
  llbuildCommands
  sqlite3
  curses
  )

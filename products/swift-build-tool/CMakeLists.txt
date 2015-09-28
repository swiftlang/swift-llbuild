add_llbuild_executable(swift-build-tool
  swift-build-tool.cpp)

target_link_libraries(swift-build-tool
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3
  curses
  )

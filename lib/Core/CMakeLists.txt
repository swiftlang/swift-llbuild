add_llbuild_library(llbuildCore STATIC
  BuildDB.cpp
  BuildEngine.cpp
  BuildEngineTrace.cpp
  DependencyInfoParser.cpp
  MakefileDepsParser.cpp
  SQLiteBuildDB.cpp
)

target_link_libraries(llbuildCore PRIVATE
  llbuildBasic
  llvmSupport
  SQLite::SQLite3)

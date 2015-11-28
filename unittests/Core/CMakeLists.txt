add_llbuild_unittest(CoreTests
  BuildEngineTest.cpp
  DepsBuildEngineTest.cpp
  MakefileDepsParserTest.cpp
  )

target_link_libraries(CoreTests llbuildCore llvmSupport sqlite3 curses)

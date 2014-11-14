add_llbuild_unittest(CoreTests
  BuildEngineTest.cpp
  DependencyGraphTest.cpp
  MakefileDepsParserTest.cpp
  )

target_link_libraries(CoreTests llbuildCore)

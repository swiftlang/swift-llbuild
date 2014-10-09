add_llbuild_unittest(CoreTests
  BuildEngineTest.cpp
  DependencyGraphTest.cpp
  )

target_link_libraries(CoreTests llbuildCore)

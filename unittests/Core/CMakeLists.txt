add_llbuild_unittest(CoreTests
  DependencyGraphTest.cpp
  )

target_link_libraries(CoreTests llbuildCore)

add_llbuild_library(gtest_main STATIC
  TestMain.cpp
  )

target_link_libraries(gtest_main PRIVATE
  gtest)

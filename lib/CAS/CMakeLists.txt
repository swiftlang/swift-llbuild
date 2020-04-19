add_llbuild_library(llbuildCAS STATIC
  CASDatabase.cpp
  )

target_link_libraries(llbuildCAS PRIVATE
  llvmSupport
  llbuildBasic)

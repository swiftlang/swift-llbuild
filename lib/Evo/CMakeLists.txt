add_llbuild_library(llbuildEvo STATIC
    EvoEngine.cpp
  )

target_link_libraries(llbuildEvo PRIVATE
  llbuildCore
  llbuildBasic
  llvmSupport)

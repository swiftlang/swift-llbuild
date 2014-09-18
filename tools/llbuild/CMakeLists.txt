add_llbuild_executable(llbuild
  llbuild.cpp)

target_link_libraries(llbuild
  llbuildBasic
  llbuildNinja
  )

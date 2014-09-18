add_executable(llbuild
  llbuild.cpp)

target_link_libraries(llbuild
  llbuildBasic
  llbuildNinja
  )

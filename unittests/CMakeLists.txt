add_custom_target(UnitTests)
set_target_properties(UnitTests PROPERTIES FOLDER "Tests")

function(add_llbuild_unittest test_dirname)
  add_unittest(UnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(Basic)
add_subdirectory(Core)
add_subdirectory(BuildSystem)
add_subdirectory(Ninja)

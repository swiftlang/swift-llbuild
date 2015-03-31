add_custom_target(PerfTests)
set_target_properties(PerfTests PROPERTIES FOLDER "Tests")

add_subdirectory(Xcode/PerfTests)

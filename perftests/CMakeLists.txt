add_custom_target(PerfTests)
set_target_properties(PerfTests PROPERTIES FOLDER "Tests")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_subdirectory(Xcode/PerfTests)
endif()

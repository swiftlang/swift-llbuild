add_llbuild_library(llbuildCommands STATIC
  BuildEngineCommand.cpp
  BuildSystemCommand.cpp
  CommandLineStatusOutput.cpp
  CommandUtil.cpp
  NinjaBuildCommand.cpp
  NinjaCommand.cpp
  )

target_link_libraries(llbuildCommands PRIVATE
  llbuildBuildSystem
  llbuildCore
  llbuildNinja)

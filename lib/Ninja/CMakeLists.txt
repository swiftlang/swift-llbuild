add_llbuild_library(llbuildNinja STATIC
  Lexer.cpp
  Manifest.cpp
  ManifestLoader.cpp
  Parser.cpp
  )

target_link_libraries(llbuildNinja PRIVATE
  llbuildBasic
  llvmSupport)

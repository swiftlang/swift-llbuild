add_llbuild_library(llbuildNinja
  Lexer.cpp
  Manifest.cpp
  ManifestLoader.cpp
  Parser.cpp
  )

target_link_libraries(llbuildNinja llvmSupport)

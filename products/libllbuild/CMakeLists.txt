add_llbuild_library(libllbuild
  C-API.cpp
  SHARED
  OUTPUT_NAME llbuild)

set_property(TARGET libllbuild PROPERTY MACOSX_RPATH ON)

target_link_libraries(libllbuild
  llbuildCore
  llbuildBasic
  sqlite3
  )

include_directories(BEFORE
  ${CMAKE_CURRENT_SOURCE_DIR}/public-api)

install(DIRECTORY public-api/llbuild DESTINATION include
        FILES_MATCHING PATTERN "*.h")

install(TARGETS libllbuild
        LIBRARY DESTINATION lib)

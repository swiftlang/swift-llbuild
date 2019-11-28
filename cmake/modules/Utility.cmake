include(CMakeParseArguments)

function(append_if condition value)
  if (${condition})
    foreach(variable ${ARGN})
      set(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    endforeach(variable)
  endif()
endfunction()

macro(add_llbuild_library name)
  cmake_parse_arguments(ARG
    "SHARED" "OUTPUT_NAME" ""
    ${ARGN})
  set(ALL_FILES ${ARG_UNPARSED_ARGUMENTS})

  # Add headers to generated project files.
  if(MSVC_IDE OR XCODE)
    # Add internal headers.

    file(GLOB internal_headers *.h)
    if(internal_headers)
      set_source_files_properties(${internal_headers} PROPERTIES HEADER_FILE_ONLY ON)
      list(APPEND ALL_FILES ${internal_headers})
    endif()

    # Add public headers.

    # Find the library name by finding its relative path.
    file(RELATIVE_PATH lib_path
      ${LLBUILD_SRC_DIR}/lib/
      ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(NOT lib_path MATCHES "^[.][.]")
      file( GLOB_RECURSE headers
        ${LLBUILD_SRC_DIR}/include/llbuild/${lib_path}/*.h
      )
      set_source_files_properties(${headers} PROPERTIES HEADER_FILE_ONLY ON)

      list(APPEND ALL_FILES ${headers})
    endif()
  endif(MSVC_IDE OR XCODE)

  if (ARG_SHARED)
    add_library(${name} SHARED ${ALL_FILES})
  else()
    add_library(${name} ${ALL_FILES})
  endif()

  if(NOT ARG_OUTPUT_NAME)
    set(ARG_OUTPUT_NAME ${name})
  endif()
  set_target_properties(${name} PROPERTIES PDB_NAME lib${ARG_OUTPUT_NAME})

  if(ARG_OUTPUT_NAME)
    set_target_properties(${name}
      PROPERTIES
      OUTPUT_NAME ${ARG_OUTPUT_NAME})
  endif()
endmacro()

# Generic support for adding a unittest.
function(add_unittest test_suite test_name)
  include_directories(${LLBUILD_SRC_DIR}/utils/unittest/googletest/include)

  add_executable(${test_name} ${ARGN})
  target_link_libraries(${test_name} PRIVATE
    gtest
    gtest_main)

  add_dependencies(${test_suite} ${test_name})
  get_target_property(test_suite_folder ${test_suite} FOLDER)
  if (NOT ${test_suite_folder} STREQUAL "NOTFOUND")
    set_property(TARGET ${test_name} PROPERTY FOLDER "${test_suite_folder}")
  endif ()
endfunction()


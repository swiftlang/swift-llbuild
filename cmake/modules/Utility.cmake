function(append_if condition value)
  if (${condition})
    foreach(variable ${ARGN})
      set(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    endforeach(variable)
  endif()
endfunction()

# Set each output directory according to ${CMAKE_CONFIGURATION_TYPES}.
# Note: Don't set variables CMAKE_*_OUTPUT_DIRECTORY any more,
# or a certain builder, for eaxample, msbuild.exe, would be confused.
function(set_output_directory target bindir libdir)
  # Do nothing if *_OUTPUT_INTDIR is empty.
  if("${bindir}" STREQUAL "")
    return()
  endif()

  # moddir -- corresponding to LIBRARY_OUTPUT_DIRECTORY.
  # It affects output of add_library(MODULE).
  if(WIN32 OR CYGWIN)
    # DLL platform
    set(moddir ${bindir})
  else()
    set(moddir ${libdir})
  endif()
  if(NOT "${CMAKE_CFG_INTDIR}" STREQUAL ".")
    foreach(build_mode ${CMAKE_CONFIGURATION_TYPES})
      string(TOUPPER "${build_mode}" CONFIG_SUFFIX)
      string(REPLACE ${CMAKE_CFG_INTDIR} ${build_mode} bi ${bindir})
      string(REPLACE ${CMAKE_CFG_INTDIR} ${build_mode} li ${libdir})
      string(REPLACE ${CMAKE_CFG_INTDIR} ${build_mode} mi ${moddir})
      set_target_properties(${target} PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${bi})
      set_target_properties(${target} PROPERTIES "ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${li})
      set_target_properties(${target} PROPERTIES "LIBRARY_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${mi})
    endforeach()
  else()
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${bindir})
    set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${libdir})
    set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${moddir})
  endif()
endfunction()

macro(add_llbuild_executable name)
  add_executable(${name} ${ARGN})

  set_output_directory(${name} ${LLBUILD_EXECUTABLE_OUTPUT_INTDIR} ${LLBUILD_LIBRARY_OUTPUT_INTDIR})
endmacro()

macro(add_llbuild_library name)
  set(ALL_FILES ${ARGN})

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

  add_library(${name} ${ALL_FILES})

  set_output_directory(${name} ${LLBUILD_EXECUTABLE_OUTPUT_INTDIR} ${LLBUILD_LIBRARY_OUTPUT_INTDIR})
endmacro()

# Generic support for adding a unittest.
function(add_unittest test_suite test_name)
  include_directories(${LLBUILD_SRC_DIR}/utils/unittest/googletest/include)

  add_llbuild_executable(${test_name} ${ARGN})
  set_output_directory(${test_name} ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR} "unused")
  target_link_libraries(${test_name} gtest gtest_main)

  add_dependencies(${test_suite} ${test_name})
  get_target_property(test_suite_folder ${test_suite} FOLDER)
  if (NOT ${test_suite_folder} STREQUAL "NOTFOUND")
    set_property(TARGET ${test_name} PROPERTY FOLDER "${test_suite_folder}")
  endif ()

  set_property(TARGET ${test_name} APPEND PROPERTY COMPILE_DEFINITIONS GTEST_HAS_RTTI=0)
endfunction()

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

  set_output_directory(${name} ${LLBUILD_EXECUTABLE_OUTPUT_INTDIR} ${LLBUILD_LIBRARY_OUTPUT_INTDIR})

  if(ARG_OUTPUT_NAME)
    set_target_properties(${name}
      PROPERTIES
      OUTPUT_NAME ${ARG_OUTPUT_NAME})
  endif()
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
endfunction()

# Compile swift sources to a dynamic framework.
# Usage:
# target          # Target name
# name            # Swift Module name
# deps            # Target dependencies
# sources         # List of sources
# additional_args # List of additional args to pass
function(add_swift_module target name deps sources additional_args)
  
  set(BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY})
  
  list(APPEND ARGS -module-name ${name})
  list(APPEND ARGS -incremental -emit-dependencies -emit-module)
  list(APPEND ARGS -emit-module-path ${name}.swiftmodule)
  
  set(FILEMAP ${BUILD_DIR}/output-file-map.json)
  set(OUTPUT_FILE_MAP ${FILEMAP})
  
  # Remove old file and start writing new one.
  file(REMOVE ${FILEMAP})
  file(APPEND ${FILEMAP} "{\n")
  foreach(source ${sources})
    file(APPEND ${FILEMAP} "\"${CMAKE_CURRENT_SOURCE_DIR}/${source}\": {\n")
    file(APPEND ${FILEMAP} "\"dependencies\": \"${BUILD_DIR}/${source}.d\",\n")
    set(OBJECT ${BUILD_DIR}/${source}.o)
    list(APPEND OUTPUTS ${OBJECT})
    file(APPEND ${FILEMAP} "\"object\": \"${OBJECT}\",\n")
    file(APPEND ${FILEMAP} "\"swiftmodule\": \"${BUILD_DIR}/${source}~partial.swiftmodule\",\n")
    file(APPEND ${FILEMAP} "\"swift-dependencies\": \"${BUILD_DIR}/${source}.swiftdeps\"\n},\n")
  endforeach()
  file(APPEND ${FILEMAP} "\"\": {\n")
  file(APPEND ${FILEMAP} "\"swift-dependencies\": \"${BUILD_DIR}/master.swiftdeps\"\n")
  file(APPEND ${FILEMAP} "}\n")
  file(APPEND ${FILEMAP} "}")
    
  list(APPEND ARGS -output-file-map ${OUTPUT_FILE_MAP})
  list(APPEND ARGS -parse-as-library)
  list(APPEND ARGS -c)

  foreach(source ${sources})
      list(APPEND ARGS ${CMAKE_CURRENT_SOURCE_DIR}/${source})
  endforeach()

  # FIXME: Find a better way to handle build types.
  if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    list(APPEND ARGS -g)
  endif()
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND ARGS -Onone -g)    
  else()
    list(APPEND ARGS -O -whole-module-optimization)
  endif()

  foreach(arg ${additional_args})
    list(APPEND ARGS ${arg})
  endforeach()
  
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND ARGS -sdk ${CMAKE_OSX_SYSROOT})
  endif()
  
  # Compile swiftmodule.
  add_custom_command(
      OUTPUT    ${OUTPUTS}
      COMMAND   swiftc
      ARGS      ${ARGS}
      DEPENDS   ${sources}
  )
  
  # Link and create dynamic framework.
  set(DYLIB_OUTPUT ${LLBUILD_LIBRARY_OUTPUT_INTDIR}/${target}.dylib)
  
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND DYLYB_ARGS -sdk ${CMAKE_OSX_SYSROOT})
  endif()
  
  list(APPEND DYLYB_ARGS -module-name ${name})
  list(APPEND DYLYB_ARGS -o ${DYLIB_OUTPUT})
  list(APPEND DYLYB_ARGS -emit-library ${OUTPUTS})
  foreach(arg ${additional_args})
    list(APPEND DYLYB_ARGS ${arg})
  endforeach()
  list(APPEND DYLYB_ARGS -L ${LLBUILD_LIBRARY_OUTPUT_INTDIR})
  
  add_custom_command(
      OUTPUT    ${DYLIB_OUTPUT}
      COMMAND   swiftc
      ARGS      ${DYLYB_ARGS}
      DEPENDS   ${OUTPUTS}
  )
  
  # Add the target.    
  add_custom_target(${target} ALL DEPENDS ${deps} ${DYLIB_OUTPUT} ${sources})
endfunction()

include(ProcessorCount)

# Compute the number of processors.
ProcessorCount(NUM_PROCESSORS)

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
    list(APPEND ARGS -num-threads ${NUM_PROCESSORS})
  endif()

  foreach(arg ${additional_args})
    list(APPEND ARGS ${arg})
  endforeach()

  # Enable autolinking so clients that import this library automatically get the
  # -l<library-name> flag.
  list(APPEND ARGS -module-link-name ${name})
  
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND ARGS -sdk ${CMAKE_OSX_SYSROOT})
  endif()
  
  # Compile swiftmodule.
  add_custom_command(
      OUTPUT    ${OUTPUTS}
      COMMAND   ${SWIFTC_EXECUTABLE}
      ARGS      ${ARGS}
      DEPENDS   ${sources} ${SWIFT_VERSION}
  )
  
  # Link and create dynamic framework.
  if(CMAKE_SYSTEM_NAME STREQUAL Windows)
    set(DYLIB_OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_SHARED_LIBRARY_PREFIX}${name}${CMAKE_SHARED_LIBRARY_SUFFIX})
  else()
    set(DYLIB_OUTPUT ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${CMAKE_SHARED_LIBRARY_PREFIX}${name}${CMAKE_SHARED_LIBRARY_SUFFIX})
  endif()
  
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND DYLYB_ARGS -sdk ${CMAKE_OSX_SYSROOT})
  endif()
  
  list(APPEND DYLYB_ARGS -module-name ${name})
  list(APPEND DYLYB_ARGS -o ${DYLIB_OUTPUT})
  list(APPEND DYLYB_ARGS -emit-library ${OUTPUTS})
  foreach(arg ${additional_args})
    list(APPEND DYLYB_ARGS ${arg})
  endforeach()

  # Add rpath to lookup the linked dylibs adjacent to itself.
  if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    list(APPEND DYLYB_ARGS -Xlinker -rpath -Xlinker @loader_path)
    list(APPEND DYLYB_ARGS -Xlinker -install_name -Xlinker @rpath/${target}${CMAKE_SHARED_LIBRARY_SUFFIX})

    # Runpath for finding Swift core libraries in the toolchain.
    # FIXME: Ideally, this should be passed from the swift-ci invocation.
    list(APPEND DYLYB_ARGS -Xlinker -rpath -Xlinker @loader_path/../../macosx)
  elseif(CMAKE_SYSTEM_NAME STREQUAL Linux)
    list(APPEND DYLYB_ARGS -Xlinker "-rpath=\\$$ORIGIN")
    list(APPEND DYLYB_ARGS -Xlinker "-rpath=\\$$ORIGIN/../../linux")
  elseif(CMAKE_SYSTEM_NAME STREQUAL Windows)
    # NOTE Windows does not support RPATH
  else()
    message(SEND_ERROR "do not know how to setup RPATH for ${CMAKE_SYSTEM_NAME}")
  endif()

  list(APPEND DYLYB_ARGS -L ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
  
  add_custom_command(
      OUTPUT    ${DYLIB_OUTPUT}
      COMMAND   ${SWIFTC_EXECUTABLE}
      ARGS      ${DYLYB_ARGS}
      DEPENDS   ${OUTPUTS}
  )
  
  # Add the target.    
  add_custom_target(${target} ALL DEPENDS ${deps} ${DYLIB_OUTPUT} ${sources} swiftversion)
endfunction()

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
  add_library(${name} ${ARGN})

  set_output_directory(${name} ${LLBUILD_EXECUTABLE_OUTPUT_INTDIR} ${LLBUILD_LIBRARY_OUTPUT_INTDIR})
endmacro()

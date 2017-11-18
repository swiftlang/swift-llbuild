# Set up some build variables used to configure the output directories (which
# may be dynamic in some generators, like Xcode).
if (CMAKE_CFG_INTDIR STREQUAL ".")
  set(LLBUILD_BUILD_MODE ".")
else ()
  set(LLBUILD_BUILD_MODE "%(build_mode)s")
endif ()

string(REPLACE ${CMAKE_CFG_INTDIR} ${LLBUILD_BUILD_MODE} LLBUILD_TOOLS_DIR ${LLBUILD_EXECUTABLE_OUTPUT_INTDIR})
string(REPLACE ${CMAKE_CFG_INTDIR} ${LLBUILD_BUILD_MODE} LLBUILD_LIBS_DIR  ${LLBUILD_LIBRARY_OUTPUT_INTDIR})

# Set up local 'lit' configuration
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.in
  ${CMAKE_CURRENT_BINARY_DIR}/Unit/lit.site.cfg)

include(FindPythonInterp)
if(PYTHONINTERP_FOUND AND LIT_FOUND AND FILECHECK_FOUND)
  set(LIT_ARGS "${LLBUILD_LIT_ARGS}")
  separate_arguments(LIT_ARGS)
  
  set(build_mode ${CMAKE_BUILD_TYPE})
  if(NOT CMAKE_CFG_INTDIR STREQUAL ".")
    set(build_mode ${CMAKE_CFG_INTDIR})
  endif()

  set(lit_command
    ${PYTHON_EXECUTABLE}
    ${LIT_EXECUTABLE}
    ${LIT_ARGS}
    --param build_mode=${build_mode})

  set(test_target_dependencies
    llbuild libllbuild swift-build-tool UnitTests)

  add_custom_target(test-llbuild
    COMMAND ${lit_command} ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running llbuild tests..."
    ${cmake_3_2_USES_TERMINAL})
  set_target_properties(test-llbuild PROPERTIES FOLDER "Tests")

  add_dependencies(test-llbuild ${test_target_dependencies})

  # Add a target for running all tests.
  if(POLICY CMP0037)
    cmake_policy(SET CMP0037 OLD)
  endif(POLICY CMP0037)
  add_custom_target(test)
  add_dependencies(test test-llbuild)
  set_target_properties(test PROPERTIES FOLDER "Tests")
else()
  message(STATUS "Not configuring tests (missing requirements)")
endif()

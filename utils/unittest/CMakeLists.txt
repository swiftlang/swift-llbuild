########################################################################
# Experimental CMake build script for Google Test.
#
# Consider this a prototype.  It will change drastically.  For now,
# this is only for people on the cutting edge.
#
# To run the tests for Google Test itself on Linux, use 'make test' or
# ctest.  You can select which tests to run using 'ctest -R regex'.
# For more options, run 'ctest --help'.
########################################################################
#
# Project-wide settings

if(WIN32)
  add_definitions(-DGTEST_OS_WINDOWS=1)
endif()

# Google Test requires headers which need _ALL_SOURCE to build on AIX
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  remove_definitions("-D_XOPEN_SOURCE=700")
  add_definitions("-D_ALL_SOURCE")
endif()

if(SUPPORTS_VARIADIC_MACROS_FLAG)
  add_definitions("-Wno-variadic-macros")
endif()
if(SUPPORTS_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS_FLAG)
  add_definitions("-Wno-gnu-zero-variadic-macro-arguments")
endif()
if(CXX_SUPPORTS_COVERED_SWITCH_DEFAULT_FLAG)
  add_definitions("-Wno-covered-switch-default")
endif()

add_llbuild_library(gtest STATIC
  googletest/src/gtest-all.cc
  googlemock/src/gmock-all.cc
)

# The googletest and googlemock sources don't presently use the 'override'
# keyword, which leads to lots of warnings from -Wsuggest-override. Disable
# that warning here for any targets that link to gtest.
if(CXX_SUPPORTS_SUGGEST_OVERRIDE_FLAG)
  add_definitions("-Wno-suggest-override")
  set_target_properties(gtest PROPERTIES INTERFACE_COMPILE_OPTIONS "-Wno-suggest-override")
endif()

if (NOT LLVM_ENABLE_THREADS)
  target_compile_definitions(gtest PUBLIC GTEST_HAS_PTHREAD=0)
endif ()

target_include_directories(gtest
  PUBLIC googletest/include googlemock/include
  PRIVATE googletest googlemock
  )

add_subdirectory(UnitTestMain)

cmake_minimum_required(VERSION 3.19)

# Get the SDK path for OSX.
# Please the position of this snippet is important, as
# CMAKE_OSX_SYSROOT needs to be set before Darwin.cmake and
# Darwin-initialize.cmake are invoked so to set the build environment
# appropriately
if (CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin)
  execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
      OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

project(llbuild LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build Shared Libraries" ON)
option(BUILD_TESTING "Build tests" ON)
option(LLBUILD_ENABLE_ASSERTIONS "enable assertions in release mode" NO)

# Configure the default set of bindings to build.
set(LLBUILD_SUPPORT_BINDINGS "" CACHE STRING "bindings to build")

# Include standard CMake modules.
include(CheckCXXCompilerFlag)


# Add path for custom modules
list(APPEND CMAKE_MODULE_PATH
  ${PROJECT_SOURCE_DIR}/cmake/modules)

set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_POSITION_INDEPENDENT_CODE YES)

if(CMAKE_CONFIGURATION_TYPES AND NOT "${CMAKE_CFG_INTDIR}" STREQUAL ".")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<LOWER_CASE:${CMAKE_BUILD_TYPE}>/lib)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<LOWER_CASE:${CMAKE_BUILD_TYPE}>/lib)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<LOWER_CASE:${CMAKE_BUILD_TYPE}>/bin)
else()
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL Android)
  set(CMAKE_HAVE_LIBC_PTHREAD TRUE)
endif()
set(THREADS_PREFER_PTHREAD_FLAG FALSE)
find_package(Threads REQUIRED)

find_package(SQLite3 REQUIRED)

# Include custom modules.
include(Utility)

# Sanity check our source directory to make sure that we are not trying to
# generate an in-tree build (it pollutes the source tree with a lot of CMake
# related files).
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed.")
endif()

###
# Build Parameters

# Define the default arguments to use with 'lit', and an option for the user to
# override.
set(LIT_ARGS_DEFAULT "-sv")
if (MSVC OR XCODE)
  set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
endif()
set(LLBUILD_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

###
# Common Macros

set(LLBUILD_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LLBUILD_OBJ_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Shared output directories for executables and libraries.
set(LLBUILD_LIBDIR_SUFFIX "${LIB_SUFFIX}" CACHE STRING "Set default library folder suffix. Ex: set it to 64 and it will use lib64")

###
# Support Tools

if(BUILD_TESTING)
  find_package(Lit)
  find_package(FileCheck)
  find_package(PythonInterp)
endif()

###
# Setup compiler and project build settings

# Find includes in the source directory.
include_directories(BEFORE
  ${PROJECT_SOURCE_DIR}/include)

# Xcode: Use libc++ and c++14 using proper build settings.
if (XCODE)
  # Force usage of Clang.
  set(CMAKE_XCODE_ATTRIBUTE_GCC_VERSION "com.apple.compilers.llvm.clang.1_0"
      CACHE STRING "Xcode Compiler")
  # Use C++'11.
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++14"
      CACHE STRING "Xcode C++ Language Standard")
  # Use libc++.
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
      CACHE STRING "Xcode C++ Standard Library")
  # Enable some warnings not enabled by default.  These
  # mostly reset clang back to its default settings, since
  # Xcode passes -Wno... for many warnings that are not enabled
  # by default.
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_ABOUT_RETURN_TYPE "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_ABOUT_MISSING_NEWLINE "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNUSED_VALUE "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNUSED_VARIABLE "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_SIGN_COMPARE "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNUSED_FUNCTION "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_HIDDEN_VIRTUAL_FUNCTIONS "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNINITIALIZED_AUTOS "YES")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_DOCUMENTATION_COMMENTS "YES")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_BOOL_CONVERSION "YES")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_EMPTY_BODY "YES")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_ENUM_CONVERSION "YES")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_INT_CONVERSION "YES")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_CONSTANT_CONVERSION "YES")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_NON_VIRTUAL_DESTRUCTOR "YES")

  # Disable RTTI.
  set(CMAKE_XCODE_ATTRIBUTE_GCC_ENABLE_CPP_RTTI "NO")

  # Disable exceptions.
  set(CMAKE_XCODE_ATTRIBUTE_GCC_ENABLE_CPP_EXCEPTIONS "NO") 

  # Disable headermaps.
  set(CMAKE_XCODE_ATTRIBUTE_USE_HEADERMAP "NO") 
elseif(MSVC)
  add_compile_options(
    # Disable unknown pragma warnings (e.g. for #pragma mark).
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4068>"

    # TODO: these warnings come from llvmSupport. Since we don't want to diverge from llvmSupport by
    # addressing these warnings , disable these for now to clean the build log.
    # If/when we move to use LLVM's own llvmSupport, we should reenable these warnings.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4141>" # 'inline' used more than once.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4146>" # Unary minus applied to unsigned type.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4244>" # Signed conversion.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4267>" # Possible loss of data conversions.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4291>" # Operator new with no matching delete found.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4800>" # Forcing value to bool 'true' or 'false'.
    "$<$<COMPILE_LANGUAGE:CXX>:-wd4996>" # POSIX name for this item is deprecated.
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
else ()
  add_compile_options(
    # Enable additional Clang warnings.
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wbool-conversion>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wconstant-conversion>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wdeprecated-declarations>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wdocumentation>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wempty-body>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wenum-conversion>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wimplicit-fallthrough>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wint-conversion>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Winvalid-offsetof>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wmost>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wnewline-eof>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wparentheses>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wsign-compare>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wswitch>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wuninitialized>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wunused-function>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wunused-value>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wunused-variable>")
endif ()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  add_compile_definitions("LLVM_ON_WIN32")
endif()

# On Linux, we may need to include a workaround for legacy libstdc++.
if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>${LLBUILD_SRC_DIR}/include/libstdc++14-workaround.h>)
endif()

# Check for -Wunreachable-code-aggressive instead of -Wunreachable-code, as that indicates
# that we have the newer -Wunreachable-code implementation.
check_cxx_compiler_flag("-Werror -Wunreachable-code-aggressive" CXX_SUPPORTS_UNREACHABLE_CODE_FLAG)
if(CXX_SUPPORTS_UNREACHABLE_CODE_FLAG)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wunreachable-code>)
endif()

# Release configuration has assertions disabled, enable them if asked to.
if(LLBUILD_ENABLE_ASSERTIONS)
  # NOTE(compnerd) theoretically, we should be able to use `add_definitions` but
  # the generator expression support prevents that.
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-UNDEBUG>)
endif()

###

# Process CMakeLists files for our subdirectories.
add_subdirectory(lib)
add_subdirectory(products)
if(BUILD_TESTING)
  add_subdirectory(tests)
  add_subdirectory(perftests)
  add_subdirectory(unittests)
  add_subdirectory(utils/unittest)
  add_subdirectory(utils/adjust-times)
endif()
add_subdirectory(cmake/modules)

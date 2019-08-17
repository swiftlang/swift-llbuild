cmake_minimum_required(VERSION 3.4.3)

project(llbuild LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build Shared Libraries" ON)

# Configure the default set of bindings to build.
set(LLBUILD_SUPPORT_BINDINGS "" CACHE STRING "bindings to build")

# Include standard CMake modules.
include(CMakeParseArguments)
include(CheckCXXCompilerFlag)

# Get the SDK path for OSX.
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE)

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

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
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

find_package(Lit)
find_package(FileCheck)
find_package(PythonInterp)

# Check if we should build the Swift bindings.
if (";${LLBUILD_SUPPORT_BINDINGS};" MATCHES ";Swift;")
  if (SWIFTC_EXECUTABLE)
    set(SWIFTC_FOUND TRUE)
    message(STATUS "Using provided swiftc: ${SWIFTC_EXECUTABLE}")
  # Find swiftc on OSX using `xcrun --find swiftc` and `find_package` on Linux.
  elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    execute_process(
        COMMAND xcrun --find swiftc
        OUTPUT_VARIABLE SWIFTC_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(SWIFTC_FOUND TRUE)
    message(STATUS "Found swiftc: ${SWIFTC_EXECUTABLE}")
  else()
    find_package(Swiftc)
  endif()
else()
  message(STATUS "Not building Swift bindings")
  set(SWIFTC_EXECUTABLE)
  set(SWIFTC_FOUND FALSE)
endif()

###
# Setup compiler and project build settings

# Find includes in the source directory.
include_directories(BEFORE
  ${CMAKE_SOURCE_DIR}/include)

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
    "-wd4068"

    # TODO: these warnings come from llvmSupport. Since we don't want to diverge from llvmSupport by
    # addressing these warnings , disable these for now to clean the build log.
    # If/when we move to use LLVM's own llvmSupport, we should reenable these warnings.
    "-wd4141" # 'inline' used more than once.
    "-wd4146" # Unary minus applied to unsigned type.
    "-wd4244" # Signed conversion.
    "-wd4267" # Possible loss of data conversions.
    "-wd4291" # Operator new with no matching delete found.
    "-wd4800" # Forcing value to bool 'true' or 'false'.
    "-wd4996" # POSIX name for this item is deprecated.
    "/EHsc")
else ()
  add_compile_options(
    # Enable additional Clang warnings.
   "-Wmost"
   "-Wdocumentation" "-Woverloaded-virtual"
   "-Wparentheses" "-Wswitch"
   "-Wunused-function" "-Wunused-variable"
   "-Wunused-value" "-Wempty-body"
   "-Wuninitialized" "-Wconstant-conversion"
   "-Wint-conversion" "-Wbool-conversion"
   "-Wenum-conversion" "-Wsign-compare"
   "-Wnewline-eof" "-Wdeprecated-declarations"
   "-Winvalid-offsetof"
   "-Wnon-virtual-dtor"
   "-Wimplicit-fallthrough")
endif ()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  add_compile_definitions("LLVM_ON_WIN32")
endif()

# On Linux, we may need to include a workaround for legacy libstdc++.
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include \"${LLBUILD_SRC_DIR}/include/libstdc++14-workaround.h\"")
endif ()

# Check for -Wunreachable-code-aggressive instead of -Wunreachable-code, as that indicates
# that we have the newer -Wunreachable-code implementation.
check_cxx_compiler_flag("-Werror -Wunreachable-code-aggressive" CXX_SUPPORTS_UNREACHABLE_CODE_FLAG)
append_if(CXX_SUPPORTS_UNREACHABLE_CODE_FLAG "-Wunreachable-code" CMAKE_CXX_FLAGS)

# Release configuration has assertions disabled, enable them if asked to.
if(LLBUILD_ENABLE_ASSERTIONS)
  string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif()

# Add an always out-of-date target to get the Swift compiler version.
if (SWIFTC_FOUND)
  set(SWIFT_VERSION ${LLBUILD_OBJ_DIR}/swift-version)
  add_custom_target(
    swiftversion ALL
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/utils/write-swift-version ${SWIFTC_EXECUTABLE} ${SWIFT_VERSION}
    BYPRODUCTS ${SWIFT_VERSION}
    COMMENT "Checking Swift compiler version"
  )
endif()

###

# Process CMakeLists files for our subdirectories.
add_subdirectory(lib)
add_subdirectory(perftests)
add_subdirectory(products)
add_subdirectory(tests)
add_subdirectory(unittests)
add_subdirectory(utils/unittest)

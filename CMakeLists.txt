cmake_minimum_required(VERSION 2.8)

# Default to building the Debug configuration.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug")
endif()

if(CMAKE_VERSION VERSION_LESS 3.1.20141117)
  set(cmake_3_2_USES_TERMINAL)
else()
  set(cmake_3_2_USES_TERMINAL USES_TERMINAL)
endif()

# Include standard CMake modules.
include(CMakeParseArguments)
include(CheckCXXCompilerFlag)

# Get the SDK path for OSX.
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE)

project(llbuild)

# Add path for custom modules
set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  )

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
set(LLBUILD_EXECUTABLE_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLBUILD_LIBRARY_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

###
# Support Tools

find_package(Lit)
find_package(FileCheck)
find_package(PythonInterp)

###
# Setup compiler and project build settings

# Find includes in the source directory.
include_directories(BEFORE
  ${CMAKE_SOURCE_DIR}/include)

# On FreeBSD, /usr/local/* is not used by default. In order to build LLVM
# with sqlite3, etc., we must add /usr/local paths.
if(${CMAKE_SYSTEM_NAME} MATCHES "(FreeBSD|DragonFly)")
  include_directories("/usr/local/include")
  link_directories("/usr/local/lib")
endif(${CMAKE_SYSTEM_NAME} MATCHES "(FreeBSD|DragonFly)")

if(NOT ${LLBUILD_PATH_TO_SQLITE_SOURCE} STREQUAL "")
  include_directories("${LLBUILD_PATH_TO_SQLITE_SOURCE}")
endif()

if(NOT "${LLBUILD_PATH_TO_SQLITE_BUILD}" STREQUAL "")
  link_directories("${LLBUILD_PATH_TO_SQLITE_BUILD}")
endif()

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
  # Disable unknown pragma warnings (e.g. for #pragma mark).
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4068")

  # TODO: these warnings come from llvmSupport. Since we don't want to diverge from llvmSupport by
  # addressing these warnings , disable these for now to clean the build log.
  # If/when we move to use LLVM's own llvmSupport, we should reenable these warnings.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4996") # POSIX name for this item is deprecated.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4244") # Signed conversion.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4267") # Possible loss of data conversions.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4291") # Operator new with no matching delete found.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4800") # Forcing value to bool 'true' or 'false'.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4146") # Unary minus applied to unsigned type.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4141") # 'inline' used more than once.
else ()
  # Compile with C++14, without RTTI or exceptions.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fno-rtti -fno-exceptions")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
  # Enable additional Clang warnings.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wmost")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdocumentation  -Woverloaded-virtual")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wparentheses -Wswitch")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-function -Wunused-variable")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-value -Wempty-body")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wuninitialized -Wconstant-conversion")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wint-conversion -Wbool-conversion")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wenum-conversion -Wsign-compare")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnewline-eof -Wdeprecated-declarations")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Winvalid-offsetof")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor")
endif ()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLLVM_ON_WIN32")
endif()

# On BSD and Linux, always build with PIC.
if(${CMAKE_SYSTEM_NAME} MATCHES ".*BSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif ()

# On Linux, we may need to include a workaround for legacy libstdc++.
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include \"${LLBUILD_SRC_DIR}/include/libstdc++14-workaround.h\"")
endif ()

# Add additional warnings.
check_cxx_compiler_flag("-Werror -Wdocumentation" CXX_SUPPORTS_DOCUMENTATION_FLAG)
append_if(CXX_SUPPORTS_DOCUMENTATION_FLAG "-Wdocumentation" CMAKE_CXX_FLAGS)

check_cxx_compiler_flag("-Werror -Wimplicit-fallthrough" CXX_SUPPORTS_IMPLICIT_FALLTHROUGH_FLAG)
append_if(CXX_SUPPORTS_IMPLICIT_FALLTHROUGH_FLAG "-Wimplicit-fallthrough" CMAKE_CXX_FLAGS)

# Check for -Wunreachable-code-aggressive instead of -Wunreachable-code, as that indicates
# that we have the newer -Wunreachable-code implementation.
check_cxx_compiler_flag("-Werror -Wunreachable-code-aggressive" CXX_SUPPORTS_UNREACHABLE_CODE_FLAG)
append_if(CXX_SUPPORTS_UNREACHABLE_CODE_FLAG "-Wunreachable-code" CMAKE_CXX_FLAGS)

check_cxx_compiler_flag("-Werror -Woverloaded-virtual" CXX_SUPPORTS_OVERLOADED_VIRTUAL)
append_if(CXX_SUPPORTS_OVERLOADED_VIRTUAL "-Woverloaded-virtual" CMAKE_CXX_FLAGS)

###

# Process CMakeLists files for our subdirectories.
add_subdirectory(lib)
add_subdirectory(perftests)
add_subdirectory(products)
add_subdirectory(tests)
add_subdirectory(unittests)
add_subdirectory(utils/unittest)

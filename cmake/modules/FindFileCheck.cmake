# Usage: find_package(FileCheck)
#
# If successful the following variables will be defined
# FILECHECK_FOUND
# FILECHECK_EXECUTABLE

find_program(FILECHECK_EXECUTABLE
             NAMES FileCheck
             DOC "Path to 'FileCheck' executable")

# Handle REQUIRED and QUIET arguments, this will also set FILECHECK_FOUND to true
# if FILECHECK_EXECUTABLE exists.
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FileCheck
                                  "Failed to locate 'FileCheck' executable"
                                  FILECHECK_EXECUTABLE)

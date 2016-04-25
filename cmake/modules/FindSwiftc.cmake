# Usage: find_package(Swiftc)
#
# If successful the following variables will be defined
# SWIFTC_FOUND
# SWIFTC_EXECUTABLE

find_program(SWIFTC_EXECUTABLE
             NAMES swiftc
             DOC "Path to 'swiftc' executable")

# Handle REQUIRED and QUIET arguments, this will also set SWIFTC_FOUND to true
# if SWIFTC_EXECUTABLE exists.
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Swiftc
                                  "Failed to locate 'swiftc' executable"
                                  SWIFTC_EXECUTABLE)

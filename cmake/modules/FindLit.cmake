# Usage: find_package(lit)
#
# If successful the following variables will be defined
# LIT_FOUND
# LIT_EXECUTABLE

find_program(LIT_EXECUTABLE
             NAMES lit
             DOC "Path to 'lit' executable")

# Handle REQUIRED and QUIET arguments, this will also set LIT_FOUND to true
# if LIT_EXECUTABLE exists.
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lit
                                  "Failed to locate 'lit' executable"
                                  LIT_EXECUTABLE)

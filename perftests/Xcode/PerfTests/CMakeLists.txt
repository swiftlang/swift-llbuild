# Find the Mac OS X platform path.
execute_process(
  COMMAND "xcrun" "--sdk" "macosx" "--show-sdk-platform-path"
  OUTPUT_VARIABLE MACOSX_PLATFORM_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Find the Mac OS X SDK path.
execute_process(
  COMMAND "xcrun" "--sdk" "macosx" "--show-sdk-path"
  OUTPUT_VARIABLE MACOSX_SDK_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

add_definitions("-isysroot" "${MACOSX_SDK_PATH}")
add_definitions("-DSRCROOT=\"${LLBUILD_SRC_DIR}\"")
add_definitions("-DTEST_TEMPS_PATH=\"${CMAKE_CURRENT_BINARY_DIR}/test-temps\"")
add_definitions("-F" "${MACOSX_PLATFORM_PATH}/Developer/Library/Frameworks")

add_library(XcodePerfTests
  MODULE
  CorePerfTests.mm
  NinjaPerfTests.mm
  BuildSystemPerfTests.mm)

set_target_properties(XcodePerfTests
  PROPERTIES
  BUNDLE true
  BUNDLE_EXTENSION xctest)

target_link_libraries(XcodePerfTests
  llvmSupport
  llbuildBasic
  llbuildCore
  llbuildNinja
  llbuildBuildSystem
  llbuildCommands
  curses
  sqlite3
  "${MACOSX_SDK_PATH}/System/Library/Frameworks/Foundation.framework"
  "${MACOSX_PLATFORM_PATH}/Developer/Library/Frameworks/XCTest.framework"
  )

# Run a custom command to fixup CMake's broken bundle CFBundleExecutable.
#
# http://www.cmake.org/Bug/view.php?id=15485
add_custom_command(
  COMMAND defaults write ${CMAKE_CURRENT_BINARY_DIR}/XcodePerfTests.xctest/Contents/Info.plist CFBundleExecutable XcodePerfTests && touch ${CMAKE_CURRENT_BINARY_DIR}/fixup-has-run
  COMMENT "Fixing up XcodePerfTests.xctest CFBundleExecutable..."
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/XcodePerfTests.xctest/Contents/Info.plist)
add_custom_target(fixup-XcodePerfTests-bundle
  ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/XcodePerfTests.xctest/Contents/Info.plist)


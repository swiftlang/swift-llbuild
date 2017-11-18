# Set sources.
set(SOURCES llbuild.swift)

# Link C API.
list(APPEND additional_args -import-underlying-module -lllbuild)
list(APPEND additional_args -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include)

# Add swift bindings target if swift compiler is present.
if (SWIFTC_FOUND)
  add_swift_module(swift-bindings llbuild libllbuild "${SOURCES}" "${additional_args}")
endif()

# Set sources.
set(SOURCES 
  BuildSystemBindings.swift
  CoreBindings.swift)

# Link C API.
list(APPEND additional_args -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/include -lllbuild)

# Add swift bindings target if swift compiler is present.
if (SWIFTC_FOUND)
  add_swift_module(libllbuildSwift llbuildSwift libllbuild "${SOURCES}" "${additional_args}")
endif()

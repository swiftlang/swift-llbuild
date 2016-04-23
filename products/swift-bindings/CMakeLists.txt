# Set sources.
set(SOURCES llbuild.swift)

# Link C API.
list(APPEND additional_args -import-underlying-module -lllbuild)
list(APPEND additional_args -I ${CMAKE_CURRENT_SOURCE_DIR}/../libllbuild/public-api)

# FIXME: Add Linux support.
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # Add swift module target
  add_swift_module(swift-bindings llbuild libllbuild "${SOURCES}" "${additional_args}")
endif()

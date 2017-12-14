add_llbuild_executable(swift-build-tool
  main.cpp)

target_link_libraries(swift-build-tool
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(swift-build-tool curses)
endif()

install(TARGETS swift-build-tool
        COMPONENT swift-build-tool
        DESTINATION bin)

add_custom_target(install-swift-build-tool
                  DEPENDS swift-build-tool
                  COMMENT "Installing swift-build-tool..."
                  COMMAND "${CMAKE_COMMAND}"
                          -DCMAKE_INSTALL_COMPONENT=swift-build-tool
                          -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")

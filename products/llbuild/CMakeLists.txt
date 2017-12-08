add_llbuild_executable(llbuild
  llbuild.cpp)

target_link_libraries(llbuild
  llbuildCommands
  llbuildNinja
  llbuildBuildSystem
  llbuildCore
  llbuildBasic
  llvmSupport
  sqlite3)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(llbuild curses)
endif()

install(TARGETS llbuild
        COMPONENT llbuild
        DESTINATION bin)

add_custom_target(install-llbuild
                  DEPENDS llbuild
                  COMMENT "Installing llbuild..."
                  COMMAND "${CMAKE_COMMAND}"
                          -DCMAKE_INSTALL_COMPONENT=llbuild
                          -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")

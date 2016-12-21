add_llbuild_library(llbuildBasic
  FileInfo.cpp
  FileSystem.cpp
  Hashing.cpp
  PlatformUtility.cpp
  SerialQueue.cpp
  Version.cpp
  ShellUtility.cpp
  )

if(${CMAKE_SYSTEM_NAME} MATCHES ".*BSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  target_link_libraries(llbuildBasic pthread)
endif()

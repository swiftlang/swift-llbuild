add_llbuild_library(llbuildBasic
  FileInfo.cpp
  Hashing.cpp
  SerialQueue.cpp
  Version.cpp
  )

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  target_link_libraries(llbuildBasic pthread)
endif()

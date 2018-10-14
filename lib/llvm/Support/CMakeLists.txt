# Disable implicit-fallthrough, if used.
append_if(CXX_SUPPORTS_IMPLICIT_FALLTHROUGH_FLAG "-Wno-implicit-fallthrough" CMAKE_CXX_FLAGS)

add_llbuild_library(llvmSupport
Allocator.cpp
Atomic.cpp
ConvertUTF.c
ConvertUTFWrapper.cpp
Debug.cpp
Errno.cpp
ErrorHandling.cpp
FileUtilities.cpp
Hashing.cpp
IntrusiveRefCntPtr.cpp
Locale.cpp
ManagedStatic.cpp
MathExtras.cpp
Memory.cpp
MemoryBuffer.cpp
Mutex.cpp
Path.cpp
Process.cpp
Program.cpp
Signals.cpp
SmallPtrSet.cpp
SmallVector.cpp
SourceMgr.cpp
StringExtras.cpp
StringMap.cpp
StringRef.cpp
Threading.cpp
TimeValue.cpp
Twine.cpp
Unicode.cpp
Valgrind.cpp
YAMLParser.cpp
circular_raw_ostream.cpp
raw_ostream.cpp
)

include(CheckAtomic)

set(DEPENDENCIES pthread)

if(NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB)
  list(APPEND DEPENDENCIES atomic)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  list(APPEND DEPENDENCIES execinfo)
  target_link_libraries(llvmSupport ${DEPENDENCIES})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  list(APPEND DEPENDENCIES dl)
  target_link_libraries(llvmSupport ${DEPENDENCIES})
endif()

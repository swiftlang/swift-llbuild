########################################################################
# CMake build script for time adjustment utility used in lit tests.
########################################################################

add_executable(adjust-times adjust-times.cpp)
target_link_libraries(adjust-times llvmSupport)


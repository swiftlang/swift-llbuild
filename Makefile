all: cmake-build

MAKE_FILE_PATH := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SRCROOT := $(abspath $(MAKE_FILE_PATH))
BUILD_DIR := $(SRCROOT)/build

cmake-build:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -G Ninja -DCMAKE_BUILD_TYPE:=Debug -DCMAKE_C_COMPILER:=clang -DCMAKE_CXX_COMPILER:=clang++ -DLLBUILD_SUPPORT_BINDINGS:=Swift $(SRCROOT)
	ninja -C $(BUILD_DIR)

cmake-test: cmake-build
	ninja -C $(BUILD_DIR) test

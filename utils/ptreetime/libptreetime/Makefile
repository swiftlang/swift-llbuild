#===- Makefile -------------------------------------------------------------===#
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2017 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
#
#===------------------------------------------------------------------------===#

CC	:= clang
ARCHS	:= x86_64
CFLAGS	:= -O3 -mmacosx-version-min=10.11
LDFLAGS :=
LIBPTREETIME_INTERPOSE_CFLAGS := $(CFLAGS)
LIBPTREETIME_INTERPOSE_LDFLAGS := $(LDFLAGS)

all: libptreetime_interpose.dylib

libptreetime_interpose.dylib: ptreetime_interpose.c
	$(CC) \
	  $(ARCHS:%=-arch %) \
	  $(LIBPTREETIME_INTERPOSE_CFLAGS) \
	  -dynamiclib \
	  -o $@ $^ $(LIBPTREETIME_INTERPOSE_LDFLAGS)

clean:
	rm -f libptreetime_interpose.dylib *~
.PHONY: clean

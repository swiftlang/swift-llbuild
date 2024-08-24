#!/bin/bash -eu
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2020 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

PROTOC_ZIP=protoc-27.2-osx-universal_binary.zip
PROTOC_URL="https://github.com/protocolbuffers/protobuf/releases/download/v27.2/$PROTOC_ZIP"

UTILITIES_DIR="$(dirname "$0")"
TOOLS_DIR="$UTILITIES_DIR/tools"

mkdir -p "$TOOLS_DIR"

if [[ ! -f "$UTILITIES_DIR/tools/$PROTOC_ZIP" ]]; then
    curl -L "$PROTOC_URL" --output "$TOOLS_DIR/$PROTOC_ZIP"
    unzip -o "$TOOLS_DIR/$PROTOC_ZIP" -d "$TOOLS_DIR"
fi

# Use swift build instead of cloning the repo to make sure that the generated code matches the SwiftProtobuf library
# being used as a dependency in the build. This might be a bit slower, but it's correct.
swift build -c release --product protoc-gen-swift --package-path "$UTILITIES_DIR/../thirdparty/swift-protobuf"

cp "$UTILITIES_DIR"/../.build/release/protoc-gen-swift "$TOOLS_DIR/bin"

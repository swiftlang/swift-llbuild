#!/usr/bin/env bash
##===----------------------------------------------------------------------===##
##
## This source file is part of the Swift open source project
##
## Copyright (c) 2026 Apple Inc. and the Swift project authors
## Licensed under Apache License v2.0 with Runtime Library Exception
##
## See http://swift.org/LICENSE.txt for license information
## See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
##
##===----------------------------------------------------------------------===##

# Installs the system dependencies needed to build and test llbuild in CI.
#
# Environment variables:
#   INSTALL_CMAKE=1  Also install the dependencies needed for the CMake build
#                    and the lit test suite (CMake, Ninja, lit, FileCheck).

set -e

# This script is not necessarily always run in a Docker container; add sudo if needed
if command -v sudo &> /dev/null && [ "$EUID" -ne 0 ] ; then
    sudo=sudo
else
    sudo=
fi

if [[ $(uname) == Darwin ]] ; then
    : # Everything needed is provided by Xcode
elif command -v apt-get >/dev/null 2>&1 ; then # bookworm, noble, jammy
    export DEBIAN_FRONTEND=noninteractive

    $sudo apt-get update -y

    # Build dependencies
    $sudo apt-get install -y libsqlite3-dev libncurses-dev

    if [[ "$INSTALL_CMAKE" == "1" ]] ; then
        $sudo apt-get install -y cmake ninja-build python3 python3-venv llvm
    fi
elif command -v dnf >/dev/null 2>&1 ; then # amazonlinux2023, rhel-ubi9
    $sudo dnf update -y

    # Build dependencies
    $sudo dnf install -y sqlite-devel ncurses-devel

    if [[ "$INSTALL_CMAKE" == "1" ]] ; then
        echo "INSTALL_CMAKE is not supported on this distribution" >&2
        exit 1
    fi
fi

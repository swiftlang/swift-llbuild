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

# Builds llbuild with CMake and runs the lit test suite (tests/), which is not
# covered by `swift test`. Expects the dependencies installed by
# `INSTALL_CMAKE=1 .github/scripts/prebuild.sh`.

set -euo pipefail

SRCROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILDROOT="${BUILDROOT:-${SRCROOT}/.build/cmake}"

# lit is not packaged consistently across distributions, so install it into a
# private virtual environment.
VENV="${BUILDROOT}/venv"
python3 -m venv "${VENV}"
"${VENV}/bin/pip" install --quiet lit

FILECHECK="$(command -v FileCheck || true)"
if [[ -z "${FILECHECK}" ]] ; then
    FILECHECK="$(ls -1d /usr/lib/llvm-*/bin/FileCheck 2>/dev/null | sort -V | tail -n 1)"
fi
if [[ -z "${FILECHECK}" ]] ; then
    echo "error: unable to find FileCheck" >&2
    exit 1
fi

cmake -G Ninja -S "${SRCROOT}" -B "${BUILDROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLBUILD_ENABLE_ASSERTIONS=ON \
    -DCMAKE_C_COMPILER="$(command -v clang)" \
    -DCMAKE_CXX_COMPILER="$(command -v clang++)" \
    -DLIT_EXECUTABLE="${VENV}/bin/lit" \
    -DFILECHECK_EXECUTABLE="${FILECHECK}" \
    -DPython3_EXECUTABLE="${VENV}/bin/python3"

ninja -C "${BUILDROOT}" test

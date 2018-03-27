#!/usr/bin/env bash

# This script generates the version.h file.  It exists as a separate build
# phase so that both the framework build and tests that import it
# 'library-style' can depend on it existing

set -e

# If not in Xcode, infer SRCROOT relative to this script
if [ -z ${SRCROOT+x} ]; then
    scriptroot=$(dirname "${BASH_SOURCE[0]}")
    SRCROOT="${scriptroot}/.."
fi;

# Read the version out of the xcconfig file
if [ -z ${LLBUILD_C_API_VERSION+x} ]; then
    LLBUILD_C_API_VERSION=$(sed -n -e "s/.*LLBUILD_C_API_VERSION = \\([0-9][0-9]*\\).*/\\1/p" "${SRCROOT}/Xcode/Configs/Version.xcconfig")
fi

# Write out version.h from the template, but only update the header if it actually changed, in order to prevent spurious rebuilds
llbuild_version_tmp="$(mktemp)"
llbuild_version="${SRCROOT}/products/libllbuild/include/llbuild/version.h"
sed -e "s/\${LLBUILD_C_API_VERSION}/${LLBUILD_C_API_VERSION}/" "${SRCROOT}/products/libllbuild/include/llbuild/version.h.in" > "$llbuild_version_tmp"
if ! diff "$llbuild_version_tmp" "$llbuild_version" > /dev/null ; then
    mv "$llbuild_version_tmp" "$llbuild_version"
fi

# For use with SwiftPM, output the conditional compilation flag to stdout
echo "-Xswiftc -DLLBUILD_C_API_VERSION_${LLBUILD_C_API_VERSION}"

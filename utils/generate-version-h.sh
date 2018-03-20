#!/usr/bin/env bash

# This script generates the version.h file.  It exists as a separate build
# phase so that both the framework build and tests that import it
# 'library-style' can depend on it existing

set -e

# If not in Xcode, infer SRCROOT relative to this script
if [ -z ${SRCROOT+x} ]; then
    scriptroot=$(dirname ${BASH_SOURCE[0]})
    SRCROOT="${scriptroot}/.."
fi;

# Read the version out of the xcconfig file
if [ -z ${LLBUILD_C_API_VERSION+x} ]; then
    LLBUILD_C_API_VERSION=$(sed -n -e "s/.*LLBUILD_C_API_VERSION = \([0-9][0-9]*\).*/\1/p" "${SRCROOT}/Xcode/Configs/Version.xcconfig")
fi

# Write out version.h from the template
sed -e "s/\${LLBUILD_C_API_VERSION}/${LLBUILD_C_API_VERSION}/" "${SRCROOT}/products/libllbuild/include/llbuild/version.h.in" > "${SRCROOT}/products/libllbuild/include/llbuild/version.h"

# For use with SwiftPM, output the conditional compilation flag to stdout
echo "-Xswiftc -DLLBUILD_C_API_VERSION_${LLBUILD_C_API_VERSION}"

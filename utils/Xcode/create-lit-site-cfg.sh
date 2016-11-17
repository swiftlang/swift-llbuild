#!/usr/bin/env sh

set -e

# Amend PATH with known location of LLVM tools
BREW="$(which brew || true)"
if [ -n "${BREW}" ]; then
    PATH="$PATH:`${BREW} --prefix`/opt/llvm/bin"
fi
# Default location on Ubuntu
PATH="$PATH:/usr/lib/llvm-3.7/bin"

# If we have an included copy of FileCheck, use that.
FILECHECK="${SRCROOT}/llbuild-test-tools/utils/Xcode/FileCheck"
if [ ! -f "${FILECHECK}" ]; then
    # If not, look in the path.
    FILECHECK="$(which FileCheck || true)"
    if [ -z "${FILECHECK}" ]; then
        echo "$0: error: unable to find 'FileCheck' testing utility in path"
        exit 1
    fi
fi

mkdir -p "${BUILT_PRODUCTS_DIR}/tests/Unit"

sed < "${SRCROOT}/tests/lit.site.cfg.in" \
      > "${BUILT_PRODUCTS_DIR}/tests/lit.site.cfg" \
    -e "s=@LLBUILD_SRC_DIR@=${SRCROOT}=g" \
    -e "s=@LLBUILD_OBJ_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_TOOLS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_LIBS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@FILECHECK_EXECUTABLE@=${FILECHECK}=g"

sed < "${SRCROOT}/tests/Unit/lit.site.cfg.in" \
      > "${BUILT_PRODUCTS_DIR}/tests/Unit/lit.site.cfg" \
    -e "s=@LLBUILD_SRC_DIR@=${SRCROOT}=g" \
    -e "s=@LLBUILD_OBJ_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_TOOLS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_LIBS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_BUILD_MODE@=.=g" \
    -e "s=@FILECHECK_EXECUTABLE@=${FILECHECK}=g"

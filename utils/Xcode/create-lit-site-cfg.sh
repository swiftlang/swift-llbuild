#!/usr/bin/env sh

set -e

# Compute the FileCheck path, to our included copy.
FILECHECK=${SRCROOT}/utils/Xcode/FileCheck

mkdir -p ${BUILT_PRODUCTS_DIR}/tests/Unit

sed < ${SRCROOT}/tests/lit.site.cfg.in \
      > ${BUILT_PRODUCTS_DIR}/tests/lit.site.cfg \
    -e "s=@LLBUILD_SRC_DIR@=${SRCROOT}=g" \
    -e "s=@LLBUILD_OBJ_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_TOOLS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_LIBS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@FILECHECK_EXECUTABLE@=${FILECHECK}=g"

sed < ${SRCROOT}/tests/Unit/lit.site.cfg.in \
      > ${BUILT_PRODUCTS_DIR}/tests/Unit/lit.site.cfg \
    -e "s=@LLBUILD_SRC_DIR@=${SRCROOT}=g" \
    -e "s=@LLBUILD_OBJ_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_TOOLS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_LIBS_DIR@=${BUILT_PRODUCTS_DIR}=g" \
    -e "s=@LLBUILD_BUILD_MODE@=.=g" \
    -e "s=@FILECHECK_EXECUTABLE@=${FILECHECK}=g"

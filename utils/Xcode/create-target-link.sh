#!/usr/bin/env sh

# Create a link for the output product in the unittests subdir, which lit
# uses to find our test binary.

set -ex

mkdir -p "${BUILT_PRODUCTS_DIR}/unittests"
rm -f "${BUILT_PRODUCTS_DIR}/unittests/${PRODUCT_NAME}"
ln -s "../${PRODUCT_NAME}" "${BUILT_PRODUCTS_DIR}/unittests"

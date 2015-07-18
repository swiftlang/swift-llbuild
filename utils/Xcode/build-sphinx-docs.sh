#!/usr/bin/env sh

# This script builds the Sphinx based documentation.

set -e

python -m sphinx -b html "${SRCROOT}/docs" "${BUILT_PRODUCTS_DIR}/docs/html"

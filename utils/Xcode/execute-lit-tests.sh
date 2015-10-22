#!/usr/bin/env sh

set -e

# Always execute 'lit' via its Python module, which should have already been
# installed.
LIT="python -m lit.main"

echo "note: running llbuild tests..."
echo "note: using lit: '${LIT}'"
${LIT} -sv --no-progress "${BUILT_PRODUCTS_DIR}/tests"

#!/usr/bin/env sh

# This script ensures that the 'lit' Python module is available for import. Both
# the XCTest adaptor and the "testing" build target using the module directly to
# run the Lit based tests.

set -e

# Check if the 'lit' module is available (e.g., in their user python path).
LIT_MODULE_PATH=$(python -c "import lit; print lit.__file__" 2> /dev/null || true)
if [ -z "${LIT_MODULE_PATH}" ]; then
    # If not, attempt an automatic user easy_install of 'lit'.
    INSTALL_LOG_PATH="${BUILT_PRODUCTS_DIR}/tests/lit.install.log"
    echo "note: attempting automatic 'lit' install, see log at: '${INSTALL_LOG_PATH}'"
    if ( ! easy_install --user lit &> ${INSTALL_LOG_PATH} ); then
        echo "error: unable to automatically install, please consult \
log or install manually."
          exit 1
    fi
fi

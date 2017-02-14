#!/usr/bin/env sh

# This script ensures that the 'sphinx' documentation tools are available.

set -e

# Check if the 'sphinx' module is available (e.g., in their user python path).
SPHINX_MODULE_PATH=$(python -c "import sphinx, recommonmark; print sphinx.__file__" 2> /dev/null || true)
if [ -z "${SPHINX_MODULE_PATH}" ]; then
    # If not, attempt an automatic user easy_install of 'sphinx'.
    mkdir -p "${BUILT_PRODUCTS_DIR}"
    INSTALL_LOG_PATH="${BUILT_PRODUCTS_DIR}/sphinx.install.log"
    echo "note: attempting automatic 'sphinx' install, see log at: '${INSTALL_LOG_PATH}'"
    if ( ! easy_install --user sphinx recommonmark &> ${INSTALL_LOG_PATH} ); then
        echo "error: unable to automatically install, please consult \
log or install manually."
          exit 1
    fi
fi

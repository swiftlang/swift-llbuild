#!/usr/bin/env sh

set -e

# Find the path to 'lit', first search the path.
LIT=$(which lit || true)
if [ -z "${LIT}" ]; then
  # If not found there, see if the user has the module available (e.g., in their
  # user python path).
  LIT_MODULE_PATH=$(python -c "import lit; print lit.__file__" 2> /dev/null || true)
  # If so, execute lit by using ``python -m``.
  if [ ! -z "${LIT_MODULE_PATH}" ]; then
      LIT="python -m lit.main"
  else
      # If not, attempt an automatic user easy_install of 'lit'.
      INSTALL_LOG_PATH=${BUILT_PRODUCTS_DIR}/tests/lit.install.log
      echo "note: attempting automatic 'lit' install, see log at: '${INSTALL_LOG_PATH}'"
      if ( ! easy_install --user lit &> ${INSTALL_LOG_PATH} ); then
          echo "error: unable to automatically install, please consult \
log or install manually."
          exit 1
      fi
      LIT="python -m lit.main"
  fi
fi

echo "note: running LLBuild tests..."
echo "note: using lit: '${LIT}'"
${LIT} -sv --no-progress ${BUILT_PRODUCTS_DIR}/tests

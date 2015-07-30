#!/bin/sh

set -ex

# Compute the "run order" as the number of commits on the branch.
echo "**** Computing Revision Index ****"
num_commits=$(echo $(cd "${XCS_SOURCE_DIR}"/llbuild && git log --format=oneline | wc -l))
echo "llbuild commits: ${num_commits}"

# Extract the performance data into the LNT format.
echo "**** Converting Performance Data ****"
data_path="${TMPDIR}/performance-data.json"
"${XCS_SOURCE_DIR}/llbuild/utils/Xcode/extract-perf-data" \
  --run-order "${num_commits}" \
  --machine-name dtprojectcore \
  --output-path "${data_path}" \
  "${XCS_DERIVED_DATA_DIR}/Logs/Test/"*.xcactivitylog

# Submit the results to the local server.
echo "**** Uploading Performance Data ****"
/Library/Server/Web/Data/WebApps/perf/venv/bin/lnt submit \
  http://localhost/perf/db_llbuild/submitRun "${data_path}"


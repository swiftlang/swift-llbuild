#!/bin/sh

set -ex

# Compute the "run order" as the number of commits on the branch.
echo "**** Computing Revision Index ****"
num_commits=$(echo $(cd "${XCS_SOURCE_DIR}"/llbuild && git log --format=oneline | wc -l))
echo "llbuild commits: ${num_commits}"

# Get the current revision.
revision=$(cd "${XCS_SOURCE_DIR}"/llbuild && git log -1 --format=oneline | cut -d\  -f1)

# Extract the performance data into the LNT format.
echo "**** Converting Performance Data ****"
data_path="${TMPDIR}/performance-data.json"
"${XCS_SOURCE_DIR}/llbuild/utils/Xcode/extract-perf-data" \
  --run-order "${num_commits}" \
  --revision "${revision}" \
  --machine-name dtprojectcore \
  --output-path "${data_path}" \
  "${XCS_OUTPUT_DIR}/xcodebuild_result.bundle"

# Submit the results to the local server.
echo "**** Uploading Performance Data ****"
/Library/Server/Web/Data/WebApps/perf/venv/bin/lnt submit \
  http://localhost/perf/db_llbuild/submitRun "${data_path}"


#!/usr/bin/env python
import subprocess
import os.path
import sys

# This script writes the Swift compiler version to a file. If the file is
# already present, it'll only update if the version has changed.

if len(sys.argv) != 3:
    print("Usage: <compiler-path> <file-path>")
    exit()

swiftc = sys.argv[1]
filepath = sys.argv[2]

cmd = [swiftc, "--version"]
version_string = subprocess.check_output(cmd).replace('\n', '')

if (os.path.isfile(filepath)):
    with open(filepath, 'r') as f:
        file_content = f.read()
    if file_content != version_string:
        with open(filepath, 'w') as f:
            f.write(version_string)
else:
    with open(filepath, 'w') as f:
        filecontent = f.write(version_string)

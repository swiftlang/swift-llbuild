# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import os
import threading
import sys

_outputLock = threading.Lock()

def message(msg):
    with _outputLock:
        print >>sys.stdout, msg
def note(msg):
    with _outputLock:
        print >>sys.stderr, "note: %s" % msg
def error(msg):
    with _outputLock:
        print >>sys.stderr, "error: %s" % msg

def get_stat_info(path):
    try:
        s = os.stat(path)
    except OSError:
        return { 'path' : path,
                 'st_mode' : 0,
                 'error' : True }
    return { 'path' : path,
             'st_mode' : s.st_mode,
             'st_ino' : s.st_ino,
             'st_dev' : s.st_dev,
             'st_size' : s.st_size,
             'st_mtime' : s.st_mtime }

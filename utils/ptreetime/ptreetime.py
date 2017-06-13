#!/usr/bin/env python

# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import datetime
import inspect
import json
import optparse
import os
import re
import subprocess
import tempfile

###

def _write_message(kind, message):
    # Get the file/line where this message was generated.
    f = inspect.currentframe()
    # Step out of _write_message, and then out of wrapper.
    f = f.f_back.f_back
    file,line,_,_,_ = inspect.getframeinfo(f)
    location = '%s:%d' % (os.path.basename(file), line)

    print >>sys.stderr, '%s: %s: %s' % (location, kind, message)

note = lambda message: _write_message('note', message)
warning = lambda message: _write_message('warning', message)
error = lambda message: _write_message('error', message)
fatal = lambda message: (_write_message('fatal error', message), sys.exit(1))

###

class MultiTool(object):
    """
    This object defines a generic command line tool instance, which dynamically
    builds its commands from a module dictionary.

    Example usage::

      import multitool

      def action_foo(name, args):
          "the foo command"

          ... 

      tool = multitool.MultiTool(locals())
      if __name__ == '__main__':
        tool.main(sys.argv)

    Any function beginning with "action_" is considered a tool command. It's
    name is defined by the function name suffix. Underscores in the function
    name are converted to '-' in the command line syntax. Actions ending ith
    "-debug" are not listed in the help.
    """

    def __init__(self, locals, version=None):
        self.version = version

        # Create the list of commands.
        self.commands = dict((name[7:].replace('_','-'), f)
                             for name,f in locals.items()
                             if name.startswith('action_'))

    def usage(self, name):
        print >>sys.stderr, "Usage: %s <command> [options] ... arguments ..." %(
            os.path.basename(name),)
        print >>sys.stderr
        print >>sys.stderr, """\
Use ``%s <command> --help`` for more information on a specific command.\n""" % (
            os.path.basename(name),)
        print >>sys.stderr, "Available commands:"
        cmds_width = max(map(len, self.commands))
        for name,func in sorted(self.commands.items()):
            if name.endswith("-debug"):
                continue

            print >>sys.stderr, "  %-*s - %s" % (cmds_width, name, func.__doc__)
        sys.exit(1)

    def main(self, args=None):
        if args is None:
            args = sys.argv

        progname = os.path.basename(args.pop(0))

        # Parse immediate command line options.
        while args and args[0].startswith("-"):
            option = args.pop(0)
            if option in ("-h", "--help"):
                self.usage(progname)
            elif option in ("-v", "--version") and self.version is not None:
                print self.version
                return
            else:
                print >>sys.stderr, "error: invalid option %r\n" % (option,)
                self.usage(progname)

        if not args:
            self.usage(progname)

        cmd = args.pop(0)
        if cmd not in self.commands:
            print >>sys.stderr,"error: invalid command %r\n" % cmd
            self.usage(progname)

        self.commands[cmd]('%s %s' % (progname, cmd), args)

###
# Event descriptions

class PTreeTimeEvent(object):
    pass

class PTreeStartEvent(PTreeTimeEvent):
    def __init__(self, ts, target):
        self.timestamp = ts
        self.target = target

class PTreeStopEvent(PTreeTimeEvent):
    def __init__(self, ts):
        self.timestamp = ts

class PTreeProcCreateEvent(PTreeTimeEvent):
    def __init__(self, ts, pid, parent):
        self.timestamp = ts
        self.pid = pid
        self.parent = parent

class PTreeUserStartupEvent(PTreeTimeEvent):
    def __init__(self, ts, pid, parent, utime, stime):
        self.timestamp = ts
        self.pid = pid
        self.parent = parent
        self.user_time = utime
        self.system_time = stime

class PTreeUserExecEvent(PTreeTimeEvent):
    def __init__(self, ts, pid, parent, utime, stime, args):
        self.timestamp = ts
        self.pid = pid
        self.parent = parent
        self.user_time = utime
        self.system_time = stime
        self.args = args

class PTreeUserSpawnEvent(PTreeTimeEvent):
    def __init__(self, ts, pid, parent, utime, stime, args):
        self.timestamp = ts
        self.pid = pid
        self.parent = parent
        self.args = args

class PTreeUserExitEvent(PTreeTimeEvent):
    def __init__(self, ts, pid, parent, utime, stime):
        self.timestamp = ts
        self.pid = pid
        self.parent = parent
        self.user_time = utime
        self.system_time = stime

class PTreeProcExitEvent(PTreeTimeEvent):
    def __init__(self, ts, name, pid, parent):
        self.timestamp = ts
        self.name = name
        self.pid = pid
        self.parent = parent

###

_event_classes = {
    'START' : PTreeStartEvent,
    'proc:::create' : PTreeProcCreateEvent,
    'user startup' : PTreeUserStartupEvent,
    'user exec' : PTreeUserExecEvent,
    'user spawn' : PTreeUserSpawnEvent,
    'user exit' : PTreeUserExitEvent,
    'proc:::exit' : PTreeProcExitEvent,
    'END' : PTreeStopEvent }

###

class ProcessInfo(object):
    def __init__(self, pid):
        self.pid = pid
        self.children = []
        self.name = None
        self.args = None
        self.start_timestamp = None
        self.exit_timestamp = None
        self.user_start_timestamp = None
        self.user_exit_timestamp = None
        self.user_time = None
        self.system_time = None
        self.startup_user_time = None
        self.startup_system_time = None
        self.parent = None

    @property
    def total_wall_time(self):
        return (self.exit_timestamp - self.start_timestamp) / 1000000.
    @property
    def total_user_time(self):
        if self.user_time is None:
            return 0
        return (self.user_time[0] + self.user_time[1] / 1000000.)
    @property
    def total_system_time(self):
        if self.system_time is None:
            return 0
        return (self.system_time[0] + self.system_time[1] / 1000000.)
    @property
    def startlag(self):
        if self.user_start_timestamp is None:
            return 0
        return (self.user_start_timestamp - self.start_timestamp) / 1000000.
    @property
    def exitlag(self):
        if self.user_exit_timestamp is None:
            return 0
        return (self.exit_timestamp - self.user_exit_timestamp) / 1000000.

    @property
    def start_timestamp_in_seconds(self):
        return self.start_timestamp / 1000000.
    @property
    def exit_timestamp_in_seconds(self):
        return self.exit_timestamp / 1000000.

    def todata(self):
        return { 'pid' : self.pid,
                 'name' : self.name,
                 'children' : [c.todata()
                               for c in self.children],
                 'start_timestamp' : self.start_timestamp,
                 'exit_timestamp' : self.exit_timestamp,
                 'user_start_timestamp' : self.user_start_timestamp,
                 'user_exit_timestamp' : self.user_exit_timestamp,
                 'user_time' : self.user_time,
                 'system_time' : self.system_time }

###


def _parse_event_log(path):
    # Load the file as JSON.
    with open(path) as f:
        data = json.load(f)
        command = data['command']
        events_data = data['events']

    # Convert the events data into events.
    def convert_event(data):
        evt_name = data.pop('evt', '').strip()
        evt_class = _event_classes.get(evt_name)
        if evt_class is None:
            raise ValueError("unknown event: %r" % ((evt_name, data),))

        try:
            return evt_class(**data)
        except TypeError:
            raise ValueError("invalid event: %r (unable to instantiate %r)" % (
                    (evt_name, data), evt_class))
    events = [convert_event(d)
              for d in events_data]

    # Order all of the events by timestamp.
    events.sort(key = lambda event: event.timestamp)

    # Check that the file is complete.
    if not events:
        raise ValueError("data file has no events: %r" % (path,))
    if not isinstance(events[0], PTreeStartEvent):
        raise ValueError("data file %r starts with unexpected event %r" % (
              path, events[0]))
    if not isinstance(events[-1], PTreeStopEvent):
        raise ValueError("data file %r ends with unexpected event %r" % (
              path, events[-1]))

    # Get the target PID.
    target = events[0].target
    target_start_timestamp = events[0].timestamp

    # Drop the start and stop events.
    events = events[1:-1]

    # Build the process tree list.
    def get_record_for_pid(pid):
        # Lookup the PID.
        record = active_processes.get(pid)
        if record is not None:
            return record

        # The PID was unknown, create a record for it.
        active_processes[pid] = record = ProcessInfo(pid)

        # Add to the list of all records.
        all_records.append(record)
        roots.add(record)

        return record

    all_records = []
    roots = set()
    target_root = None
    active_processes = {}
    active_spawn_args = {}
    for event in events:
        # Handle the exit of the target process specially.
        if event.pid == target:
            if not isinstance(event, PTreeProcExitEvent):
                raise RuntimeError("unexpected event for target %r" % (
                        event,))

            assert isinstance(event, PTreeProcExitEvent)
            target_root = get_record_for_pid(event.pid)
            roots.remove(target_root)
            target_root.start_timestamp = target_start_timestamp
            target_root.exit_timestamp = event.timestamp
            target_root.name = event.name
            del active_processes[event.pid]
            continue

        # First, lookup or create an entry for the parent process.
        parent_process = get_record_for_pid(event.parent)

        # Get the record for this PID.
        child_process = get_record_for_pid(event.pid)

        # If this is a create event, store the timestamp and add it to the
        # parent.
        if isinstance(event, PTreeProcCreateEvent):
            if child_process.parent:
                raise RuntimeError("multiple create events for %r" % (
                        event,))
            child_process.parent = parent_process
            child_process.start_timestamp = event.timestamp
            parent_process.children.append(child_process)
            roots.remove(child_process)

            child_process.args = active_spawn_args.pop(event.parent, None)
            if child_process.args is not None:
                child_process.name = os.path.basename(child_process.args[0])
            continue

        # Otherwise, handle the various "user" events.
        if isinstance(event, PTreeUserStartupEvent):
            child_process.user_start_timestamp = event.timestamp
            child_process.startup_user_time = event.user_time
            child_process.startup_system_time = event.system_time
            continue
        if isinstance(event, PTreeUserExecEvent):
            # If the child process already has arguments defined, then the
            # process is re-exec'ing itself. Create a new process entry.
            if child_process.args is not None:
                child_process.exit_timestamp = \
                    child_process.user_exit_timestamp = event.timestamp
                child_process.user_time = event.user_time
                child_process.system_time = event.system_time
                # FIXME: Get utime and stime available here, and copy.
                del active_processes[event.pid]

                child_process = get_record_for_pid(event.pid)
                child_process.parent = parent_process
                child_process.start_timestamp = event.timestamp
                child_process.args = event.args
                child_process.name = os.path.basename(child_process.args[0])
                parent_process.children.append(child_process)
                roots.remove(child_process)
            else:
                child_process.args = event.args
                child_process.name = os.path.basename(child_process.args[0])
            continue
        if isinstance(event, PTreeUserSpawnEvent):
            active_spawn_args[event.pid] = event.args
            continue
        if isinstance(event, PTreeUserExitEvent):
            child_process.user_exit_timestamp = event.timestamp
            child_process.user_time = event.user_time
            child_process.system_time = event.system_time
            continue

        # Otherwise, this is an exit event, store the event information and
        # update the parent and active process list.
        if child_process.parent is None:
            warning('found stray exit: %r' % (
                    (event.timestamp, event.pid, event.name),))
            child_process.parent = parent_process
            parent_process.children.append(child_process)
            roots.remove(child_process)
        assert isinstance(event, PTreeProcExitEvent)
        child_process.exit_timestamp = event.timestamp
        child_process.name = event.name
        del active_processes[event.pid]

    return target_root, roots, active_processes

def action_analyze(name, args):
    """analyze a ptreetime data file"""
    parser = optparse.OptionParser("""\
usage: %%prog %(name)s [options] <path>

Execute the given command and track the execution time of all the processes
which are created during the execution.""" % locals())
    parser.add_option("-f", "--focus", dest="focus",
                      help="focus on the given process name",
                      action="store", default=None)
    parser.add_option("--show-tree", dest="show_tree",
                      help="Print the entire call tree",
                      action="store_true")
    parser.add_option("", "--gen-report", dest="lnt_report_path",
                      help="generate an LNT report",
                      action="store", default=None)
    parser.add_option("", "--report-machine", dest="report_machine",
                      help="machine name to include in the LNT report",
                      action="store", default=None)
    parser.add_option("", "--report-run-order", dest="report_run_order",
                      help="run order to include in the LNT report",
                      action="store", default=None)
    parser.add_option("", "--report-sub-arg", dest="report_sub_args",
                      help="add an argument replacement pattern",
                      metavar="PATTERN=REPLACEMENT",
                      action="append", default=[])

    (opts, args) = parser.parse_args(args)

    if len(args) < 1:
       parser.error("invalid number of arguments")
    if opts.lnt_report_path:
        if opts.report_machine is None:
            parser.error("--report-machine is required with --gen-report")
        if opts.report_run_order is None:
            parser.error("--report-run-order is required with --gen-report")
    else:
        if opts.report_machine is not None:
            parser.error("--report-machine is unused without --gen-report")
        if opts.report_run_order is not None:
            parser.error("--report-run-order is unused without --gen-report")
        if opts.report_sub_args:
            parser.error("--report-sub-arg is unused without --gen-report")

    paths = args

    def find_focused_roots(node):
        # If this is a focused node, add it to the list.
        if opts.focus is None or node.name == opts.focus:
            focused_roots.append(node)
            return

        # Otherwise, recurse on each child.
        for child in node.children:
            find_focused_roots(child)
    focused_roots = []
    for path in paths:
        # Load each file.
        target_root,roots,active_processes = _parse_event_log(path)
        find_focused_roots(target_root)

    # For each root, show the flattened performance.
    def print_node(node, depth):
        # If this node isn't complete, ignore it.
        if node.start_timestamp is None:
            warning("ignoring incomplete node %r (didn't see create)" % (
                    (node.pid, node.name),))
            return
        if node.exit_timestamp is None:
            warning("ignoring incomplete node %r (didn't see exit)" % (
                    (node.pid,),))
            return

        indent = '  '*depth
        wall = node.total_wall_time
        user = node.total_user_time
        sys = node.total_system_time
        startlag = node.startlag
        exitlag = node.exitlag
        if opts.show_tree:
            print ('%s%s -- wall: %.4fs, user: %.4fs, sys: %.4fs, '
                   'startlag: %.4fs, exitlag: %.4fs') % (
                indent, node.name, wall, user, sys, startlag, exitlag)

        key = '%s @ %d' % (node.name, depth)
        aggregate_info = aggregate.get(key)
        if aggregate_info is None:
            aggregate[key] = aggregate_info = {
                'name' : key,
                'wall' : 0.0,
                'user' : 0.0,
                'sys' : 0.0,
                'startlag' : 0.0,
                'exitlag' : 0.0,
                'count' : 0 }
        aggregate_info['wall'] += wall
        aggregate_info['user'] += user
        aggregate_info['sys'] += sys
        aggregate_info['startlag'] += startlag
        aggregate_info['exitlag'] += exitlag
        aggregate_info['count'] += 1

        for c in node.children:
            print_node(c, depth+1)

    if not focused_roots:
        parser.error("no roots found!")

    aggregate = {}
    for node in focused_roots:
       print_node(node, depth=0)
       if node is not focused_roots[-1]:
          print

    print
    print 'Aggregate Times'
    print '---------------'
    items = aggregate.values()
    items.sort(key = lambda i: i['wall'],
               reverse = True)
    name_length = max(len(item['name'])
                      for item in items)
    for item in items:
       print ('%-*s -- wall: %8.4fs, user: %8.4fs, sys: %8.4fs, '
              'startlag: %8.4fs, exitlag: %.4fs, count: %d') % (
          name_length, item['name'], item['wall'], item['user'], item['sys'],
          item['startlag'], item['exitlag'], item['count'])

    # Write out an LNT test report with the data, if requested.
    if opts.lnt_report_path:
        import lnt.testing

        # Build the regular expression substitution arguments.
        substitution_args = []
        for value in opts.report_sub_args:
            if '=' in value:
                pattern,replacement = value.split('=',1)
            else:
                pattern,replacement = value,''
            substitution_args.append((re.compile(pattern), replacement))

        # First, organize all the nodes by deriving a test name key for them.
        def group_node_and_children(node, depth = 0):
            # Ignore incomplete nodes.
            if node.start_timestamp is None or node.exit_timestamp is None:
                return

            args = node.args
            if args is not None:
                # If this is a clang -cc1 invocation, do some special stuff to
                # normalize.
                if node.name == "clang" and args and args[1] == '-cc1':
                    triple_index = args.index('-triple')
                    main_file_name_index = args.index('-main-file-name')
                    args = (
                        args[:2] +
                        args[triple_index:triple_index+2] + \
                        args[main_file_name_index:main_file_name_index+2])


                # Apply substitutions to each argument.
                def substitute(arg):
                    for pattern_re,replacement in substitution_args:
                        arg = pattern_re.sub(replacement, arg)
                    return arg
                args = [substitute(arg)
                        for arg in args]

                # Eliminate empty arguments.
                args = filter(None, args)

            key = '%s(depth=%d,args=%r)' % (node.name, depth, args)
            items = nodes_by_key.get(key)
            if items is None:
                nodes_by_key[key] = items = []
            items.append(node)

            for child in node.children:
                group_node_and_children(child, depth+1)
        nodes_by_key = {}
        for node in focused_roots:
            group_node_and_children(node)

        tag = "compile"
        run_info = { "tag" : tag,
                     "run_order" : opts.report_run_order }
        machine = lnt.testing.Machine(opts.report_machine, {})
        run = lnt.testing.Run(datetime.datetime.fromtimestamp(
                target_root.start_timestamp_in_seconds).\
                                  strftime('%Y-%m-%d %H:%M:%S'),
                              datetime.datetime.fromtimestamp(
                target_root.exit_timestamp_in_seconds).\
                                  strftime('%Y-%m-%d %H:%M:%S'),
                              info=run_info)

        # We report in a scheme compatible with the compile time suite.
        testsamples = []
        for key,nodes in nodes_by_key.items():
            for subkey,accessor in (('wall', 'total_wall_time'),
                                    ('user', 'total_user_time'),
                                    ('sys', 'total_system_time')):
                values = [getattr(node, accessor)
                          for node in nodes]
                name = '%s.%s.%s' % (tag, key, subkey)
                testsamples.append(lnt.testing.TestSamples(
                        name, values))

        report = lnt.testing.Report(machine, run, testsamples)
        with open(opts.lnt_report_path, "w") as f:
            print >>f, report.render()

###

def action_profile(name, args):
    """time a process tree"""

    parser = optparse.OptionParser("""\
usage: %%prog %(name)s [options] ... test command args ...

Use dtrace and dyld interpositioning to collect precise time information on an
entire process tree.""" % locals())
    parser.add_option("-v", "--verbose", dest="verbose",
                      help="output more test information",
                      action="store_true", default=False)
    parser.add_option("-o", "--output", dest="output_path",
                      help="path for data output",
                      action="store", default=None)

    parser.disable_interspersed_args()

    (opts, args) = parser.parse_args(args)
    if opts.output_path is None:
        parser.error("--output argument is required")

    command_arguments = args

    # Check that the ptreetime interpose library has been built.
    basepath = os.path.dirname(os.path.realpath(__file__))
    ptreetime_dtrace_path = os.path.join(
        basepath, 'libptreetime', 'ptreetime.dtrace')
    interpose_path = os.path.join(
        basepath, 'libptreetime', 'libptreetime_interpose.dylib')
    interpose_enabled = os.path.exists(interpose_path)
    if not interpose_enabled:
        warning(("interpose library not built, please 'make' in directory: %r"
                 "(some information will not be gathered)") % (
                     os.path.dirname(interpose_path)))

    # First, establish temporary files to contain the dtrace log and the
    # interposed event log.
    ptreetime_dtrace_log = tempfile.NamedTemporaryFile(
        suffix='-ptreetime-dtrace.log')
    ptreetime_interpose_log = tempfile.NamedTemporaryFile(
        suffix='-ptreetime-interpose.log')

    # Write the environment overrides and execution command into a script.
    with tempfile.NamedTemporaryFile(suffix='-ptreetime.sh') as \
            ptreetime_script:
        os.chmod(ptreetime_script.name, 0755)
        print >>ptreetime_script, '#!/bin/sh'
        if interpose_enabled:
            print >>ptreetime_script, 'export PTREETIME_LOG_PATH="%s"' % (
                ptreetime_interpose_log.name)
            print >>ptreetime_script, 'export DYLD_INSERT_LIBRARIES="%s"' % (
                interpose_path)
        # FIXME: Quote arguments better.
        print >>ptreetime_script, ' '.join("'%s'" % arg
                                           for arg in command_arguments)
        ptreetime_script.flush()

        # Form the dtrace command to execute. Notably we:
        #   1. Force dtrace to set up probes at exec time.
        #   2. Extend the buffer size.
        #   3. Send dtrace logging output to the (shared) log file.
        #   4. Execute the script we created.
        dtrace_args = ['sudo', '/usr/sbin/dtrace',
                       '-xevaltime=exec', '-xbufsize=50m',
                       '-s', ptreetime_dtrace_path,
                       '-o', ptreetime_dtrace_log.name,
                       '-c', ptreetime_script.name]

        # Execute the dtrace command.
        note("executing dtrace (with sudo)...")
        if opts.verbose:
            note("executing: %r" % (' '.join(dtrace_args),))
        p = subprocess.Popen(dtrace_args)
        res = p.wait()
        if res != 0:
            warning("dtrace exited with a error (%d)" % (res,))

    # Read in the log file.
    #
    # We simply load all the events lines as text and order them by timestamp,
    # but otherwise we don't process them at all.
    prefix = "PTREETIME "
    prefix_len = len(prefix)
    note("reading dtrace event log...")
    with open(ptreetime_dtrace_log.name) as f:
        event_data_lines = [ln[prefix_len:-1]
                            for ln in f
                            if ln.startswith(prefix)]
    if interpose_enabled:
        note("reading interposed event log...")
        with open(ptreetime_interpose_log.name) as f:
            event_data_lines.extend([ln[prefix_len:-1]
                                     for ln in f
                                     if ln.startswith(prefix)])
    note("loaded %d events" % (len(event_data_lines),))

    # Delete the temporary log files.
    del ptreetime_interpose_log
    del ptreetime_dtrace_log

    # Sort the event lines. Note that this is purely to make for more readable
    # output files, the ptreetime parser will also order.
    def extract_timestamp(ln):
        # Extract the timestamp, if we can.
        timestamp = ln.split(':', 1)[1].split(',', 1)[0]
        if timestamp.isdigit():
            timestamp = int(timestamp)
        return timestamp
    event_data_lines.sort(key = extract_timestamp)

    # Write out the JSON file. We don't care to guarantee this file is well
    # formed (the consumer of the data files can handle that).
    note("writing output file: %r" % (opts.output_path,))
    with open(opts.output_path, "w") as f:
        print >>f, '{'
        print >>f, '  "command" : %s,' % (
            json.dumps(command_arguments),)
        print >>f, '  "events"  : ['
        last_line = event_data_lines[-1]
        for ln in event_data_lines:
            print >>f, '    %s%c' % (ln, ',]'[ln is last_line])
        print >>f, '}'

###

tool = MultiTool(locals(), "ptreetime")

if __name__ == '__main__':
    import sys
    tool.main(sys.argv)

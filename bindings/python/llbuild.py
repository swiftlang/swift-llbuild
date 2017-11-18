# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

"""
Bindings for the llbuild C API.

This is incomplete, and has several serious issues.
"""

import cffi
import os
import re

###
# TODO
#
# 1. Deal with leaks of Task and Rule objects, because of the handle cycle. We
#    need the C-API to allow us to do cleanup on the Task and Delegate user
#    contex pointers.

###

class AbstractError(Exception):
    pass

###

ffi = cffi.FFI()

# Load the defs by reading the llbuild header directly.
data = open(os.path.join(
    os.path.dirname(__file__),
    "../../products/libllbuild/include/llbuild/llbuild.h")).read()
# Strip out the directives.
data = re.sub("^#.*", "", data, 0, re.MULTILINE)
data = re.sub("LLBUILD_EXPORT", "", data, 0, re.MULTILINE)
ffi.cdef(data)

# Load the dylib.
#
# FIXME: Need a way to find this.
libllbuild = ffi.dlopen(os.path.join(
    os.path.dirname(__file__),
    "../../build/lib/libllbuild.dylib"))

@ffi.callback("void(void*, void*, llb_task_t*)")
def _task_start(context, engine_context, _task):
    task = ffi.from_handle(context)
    engine = ffi.from_handle(engine_context)

    task.start(engine)

@ffi.callback(
    "void(void*, void*, llb_task_t*, uintptr_t, llb_data_t*)")
def _task_provide_value(context, engine_context, task, input_id, value):
    task = ffi.from_handle(context)
    engine = ffi.from_handle(engine_context)

    task.provide_value(engine, input_id,
                       str(ffi.buffer(value.data, value.length)))
    
@ffi.callback("void(void*, void*, llb_task_t*)")
def _task_inputs_available(context, engine_context, llb_task):
    task = ffi.from_handle(context)
    engine = ffi.from_handle(engine_context)

    # Notify the task.
    task.inputs_available(engine)

@ffi.callback("llb_task_t*(void*, void*)")
def _rule_create_task(context, engine_context):
    rule = ffi.from_handle(context)
    engine = ffi.from_handle(engine_context)

    # Create the task.
    task = rule.create_task()
    assert isinstance(task, Task)

    # FIXME: Should we pass the context pointer separately from the delegate
    # structure, so it can be reused?
    delegate = ffi.new("llb_task_delegate_t*")
    delegate.context = task._handle
    delegate.start = _task_start
    delegate.provide_value = _task_provide_value
    delegate.inputs_available = _task_inputs_available

    task._task = libllbuild.llb_task_create(delegate[0])
    return libllbuild.llb_buildengine_register_task(engine._engine, task._task)

@ffi.callback("bool(void*, void*, llb_rule_t*, llb_data_t*)")
def _rule_is_result_valid(context, engine_context, rule, value):
    rule = ffi.from_handle(context)
    engine = ffi.from_handle(engine_context)

    return rule.is_result_valid(
        engine, str(ffi.buffer(value.data, value.length)))

@ffi.callback("void(void*, void*, llb_rule_status_kind_t)")
def _rule_update_status(context, engine_context, kind):
    rule = ffi.from_handle(context)
    engine = ffi.from_handle(engine_context)

    rule.update_status(engine, kind)

@ffi.callback("void(void*, llb_data_t*, llb_rule_t*)")
def _buildengine_lookup_rule(context, key, rule_out):
    engine = ffi.from_handle(context)
    rule = engine.delegate.lookup_rule(str(ffi.buffer(key.data, key.length)))

    # Initialize the rule result from the given object.
    assert isinstance(rule, Rule)

    # FIXME: What is to prevent the rule from being deallocated after this
    # point? We only are passing back a handle.
    rule_out.context = rule._handle
    rule_out.create_task = _rule_create_task
    rule_out.is_result_valid = _rule_is_result_valid
    rule_out.update_status = _rule_update_status
    
class _Data(object):
    """Wrapper for a key and its data."""
    def __init__(self, name):
        name = str(name)
        self.key = ffi.new("llb_data_t*")
        self.key.length = length = len(name)
        # Store in a local to keep alive.
        self._datap = ffi.new("char[]", length)
        self.key.data = self._datap
        # Copy in the data.
        ffi.buffer(self.key.data, length)[:] = name

class _HandledObject(object):
    _all_handles = []
    _handle_cache = None
    @property
    def _handle(self):
        # FIXME: This creates a cycle.
        if self._handle_cache is None:
            self._handle_cache = handle = ffi.new_handle(self)
            # FIXME: This leaks everything, but we are currently dropping a
            # handle somewhere.
            self._all_handles.append(handle)
        return self._handle_cache
            
###

def get_full_version():
    return ffi.string(libllbuild.llb_get_full_version_string())
        
class Task(_HandledObject):
    _task = None

    def start(self, engine):
        pass

    def provide_value(self, engine, input_id, value):
        # We consider it a runtime error for a task to receive a value if it
        # didn't override this callback.
        raise RuntimeError()

    def inputs_available(self, engine):
        raise AbstractError()

class Rule(_HandledObject):
    def create_task(self):
        raise AbstractError()

    def is_result_valid(self, engine, result):
        return True

    def update_status(self, engine, kind):
        pass

class BuildEngineDelegate(object):
    pass

class BuildEngine(_HandledObject):
    def __init__(self, delegate):
        # Store our delegate.
        self.delegate = delegate

        # Create the engine delegate.
        self._llb_delegate = ffi.new("llb_buildengine_delegate_t*")
        self._llb_delegate.context = self._handle
        self._llb_delegate.lookup_rule = _buildengine_lookup_rule

        # Create the underlying engine.
        self._engine = libllbuild.llb_buildengine_create(self._llb_delegate[0])

    ###
    # Client API

    def build(self, key):
        """
        build(key) -> value

        Compute the result for the given key.
        """

        # Create a data version of the key.
        key_data = _Data(key)

        # Request the build.
        result = ffi.new("llb_data_t*")
        libllbuild.llb_buildengine_build(self._engine, key_data.key, result)

        # Copy out the result.
        return str(ffi.buffer(result.data, result.length))

    def attach_db(self, path, schema_version=1):
        """
        attach_db(path, schema_version=1) -> None

        Attach a database to store the build results.
        """

        error = ffi.new("char *")
        path = _Data(path)
        if not libllbuild.llb_buildengine_attach_db(
                self._engine, path.key, schema_version, error):
            raise IOError("unable to attach database; %r" % (
                ffi.string(error),))
        
    def close(self):
        """
        close() -- Close the engine connection.
        """
        self._handle_cache = None
        libllbuild.llb_buildengine_destroy(self._engine)
        self._engine = None

    ###
    # Task API

    def task_needs_input(self, task, key, input_id=0):
        """\
task_needs_input(task, key, input_id)

Specify the given \arg Task depends upon the result of computing \arg key.

The result, when available, will be provided to the task via \see
provide_value(), supplying the provided \arg input_id to allow the
task to identify the particular input.

NOTE: It is an unchecked error for a task to request the same input value
multiple times.

\param input_id An arbitrary value that may be provided by the client to
use in efficiently associating this input. The range of this parameter is
intentionally chosen to allow a pointer to be provided."""
        key = _Data(key)
        libllbuild.llb_buildengine_task_needs_input(
            self._engine, task._task, key.key, input_id)

    def task_must_follow(self, task, key):
        """\
task_must_follow(task, key)

Specify that the given \arg task must be built subsequent to the
computation of \arg key.

The value of the computation of \arg key is not available to the task, and the
only guarantee the engine provides is that if \arg key is computed during a
build, then \arg Task will not be computed until after it."""
        key = _Data(key)
        libllbuild.llb_buildengine_task_must_follow(
            self._engine, task._task, key.key)

    def task_discovered_dependency(self, task, key):
        """\
task_discovered_dependency(task, key)

Inform the engine of an input dependency that was discovered by the task
during its execution, a la compiler generated dependency files.

This call may only be made after a task has received all of its inputs;
inputs discovered prior to that point should simply be requested as normal
input dependencies.

Such a dependency is not used to provide additional input to the task,
rather it is a way for the task to report an additional input which should
be considered the next time the rule is evaluated. The expected use case
for a discovered dependency is is when a processing task cannot predict
all of its inputs prior to being run, but can presume that any unknown
inputs already exist. In such cases, the task can go ahead and run and can
report the all of the discovered inputs as it executes. Once the task is
complete, these inputs will be recorded as being dependencies of the task
so that it will be recomputed when any of the inputs change.

It is legal to call this method from any thread, but the caller is
responsible for ensuring that it is never called concurrently for the same
task."""
        key = _Data(key)
        libllbuild.llb_buildengine_task_must_follow(
            self._engine, task._task, key.key)

    def task_is_complete(self, task, value, force_change=False):
        """\
task_is_complete(task, value, force_change=False)

Called by a task to indicate it has completed and to provide its value.

It is legal to call this method from any thread.

\param value The new value for the task's rule.

\param force_change If true, treat the value as changed and trigger
dependents to rebuild, even if the value itself is not different from the
prior result."""
        # Complete the result.
        value = _Data(value)
        libllbuild.llb_buildengine_task_is_complete(
            self._engine, task._task, value.key, force_change)

__all__ = ['get_full_version', 'BuildEngine', 'Rule', 'Task']

# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

"""
Simple Data-Driven Build System
"""

import json
import threading
import sys
import traceback

import llbuild

class SimpleRule(llbuild.Rule, llbuild.Task):
    """
    asKey(*args) -> str

    Return the key for computing the rule in the future.
    """
    @classmethod
    def asKey(klass, *args):
        kind = klass.__name__
        assert kind.endswith("Rule")
        kind = kind[:-4]
        return json.dumps({ "kind" : kind, "args" : args })

    def create_task(self):
        return self

    ###
    # JSON-ify Wrappers

    def is_result_valid(self, engine, result):
        try:
            return self.is_data_valid(engine, json.loads(result))
        except:
            traceback.print_exc()
            return True

    def is_data_valid(self, engine, result):
        return True

    def provide_value(self, engine, input_id, result):
        try:
            return self.provide_data(engine, input_id, json.loads(result))
        except:
            traceback.print_exc()
            return None

    def provide_data(self, engine, input_id, data):
        raise RuntimeError("missing client override")

class SimpleAsyncRule(SimpleRule):
    _async = True

    def run(self):
        abstract

    def inputs_available(self, engine):
        if self._async:
            # Spawn a thread to do the actual work.
            t = threading.Thread(target=self._execute, args=(engine,))
            t.start()
        else:
            self._execute(engine)

    def _execute(self, engine):
        try:
            result = self.run()
        except:
            traceback.print_exc()
            result = None
        engine.task_is_complete(self, result)

class DataDrivenEngine(llbuild.BuildEngine):
    def __init__(self, namespace):
        super(DataDrivenEngine, self).__init__(self)
        self.namespace = namespace

    def lookup_rule(self, name):
        # Rules are encoded as a JSON dictionary.
        data = json.loads(name)
        rule_name = data["kind"] + "Rule"
        rule_class = self.namespace.get(rule_name)
        if rule_class is None:
            raise RuntimeError("invalid rule: %r" % (data,))
        return rule_class(*data['args'])

    def task_is_complete(self, task, data, force_change=False):
        value = json.dumps(data)
        super(DataDrivenEngine, self).task_is_complete(
            task, value, force_change)

    def build(self, key):
        result = super(DataDrivenEngine, self).build(key)
        return json.loads(result)

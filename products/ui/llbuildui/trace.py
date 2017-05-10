import json

class Trace(object):
    """
    An llbuild build system trace
    """

    # Get a cached trace.
    @classmethod
    def frompath(cls, path):
        db = cls._traces.get(path)
        if db is None:
            cls._traces[path] = db = Trace(path)
        return db
    _traces = {}
    
    def __init__(self, path):
        self.events = []
        self.tasks = {}
        self.rules = {}

        # FIXME: Move this format to just JSON for ease of loading.
        with open(path) as f:
            lines = list(f)
            print((lines[0], lines[-1]))
            assert(lines.pop(0) == '[\n')
            assert(lines.pop(-1) == ']\n')
            for line in lines:
                assert(line.startswith('{ '))
                assert(line.endswith('},\n'))
                line = line[2:-3]
                event_data = [eval(s) for s in line.split(', ')]
                handler = _event_handlers.get(event_data[0])
                if handler is None:
                    raise NotImplementedError(
                        "unknown event: {}".format(event_data[0]))
                event = handler(self, event_data[1:])
                if event:
                    self.events.append(event)

# MARK: Event Parsing

class Rule(object):
    def __init__(self, data):
        (name, key) = data
        self.name = name
        self.key = key

class Task(object):
    def __init__(self, data):
        (name,) = data
        self.name = name
        self.rule = None

###

class Event(object):
    @property
    def isReadiedTask(self):
        return isinstance(self, ReadiedTask)

class BuildStarted(Event):
    def __init__(self, trace, data):
        pass

class BuildEnded(Event):
    def __init__(self, trace, data):
        pass

class HandlingBuildInputRequest(Event):
    def __init__(self, trace, data):
        (rule,) = data
        self.rule = trace.rules[rule]

class CheckingRuleNeedsToRun(Event):
    def __init__(self, trace, data):
        (rule,) = data
        self.rule = trace.rules[rule]

class RuleNeedsToRun(Event):
    class NeverBuilt(Event):
        pass
    class InvalidValue(Event):
        pass
    class InputRebuilt(Event):
        def __init__(self, inputRule):
            self.inputRule = inputRule
    def __init__(self, trace, data):
        self.rule = trace.rules[data[0]]
        if data[1] == 'invalid-value':
            (_, _) = data
            self.reason = RuleNeedsToRun.InvalidValue()
        elif data[1] == 'never-built':
            (_, _) = data
            self.reason = RuleNeedsToRun.NeverBuilt()
        elif data[1] == 'input-rebuilt':
            (_, _, inputRule) = data
            self.reason = RuleNeedsToRun.InputRebuilt(
                trace.rules[inputRule])
        else:
            raise NotImplementedError("unknown reason: {}".format(data))

class RuleDoesNotNeedToRun(Event):
    def __init__(self, trace, data):
        (rule,) = data
        self.rule = trace.rules[rule]

class CreatedTaskForRule(Event):
    def __init__(self, trace, data):
        (task, rule) = data
        self.task = trace.tasks[task]
        self.rule = trace.rules[rule]
        self.task.rule = self.rule

class HandlingTaskInputRequest(Event):
    def __init__(self, trace, data):
        (task, rule) = data
        self.task = trace.tasks[task]
        self.rule = trace.rules[rule]

class AddedRulePendingTask(Event):
    def __init__(self, trace, data):
        (rule, task) = data
        self.rule = trace.rules[rule]
        self.task = trace.tasks[task]

class RuleScheduledForScanning(Event):
    def __init__(self, trace, data):
        (rule,) = data
        self.rule = trace.rules[rule]

class PausedInputRequestForRuleScan(Event):
    def __init__(self, trace, data):
        (rule,) = data
        self.rule = trace.rules[rule]

class ReadyingTaskInputRequest(Event):
    def __init__(self, trace, data):
        (task, rule) = data
        self.task = trace.tasks[task]
        self.rule = trace.rules[rule]

class CompletedTaskInputRequest(Event):
    def __init__(self, trace, data):
        (task, rule) = data
        self.task = trace.tasks[task]
        self.rule = trace.rules[rule]

class UpdatedTaskWaitCount(Event):
    def __init__(self, trace, data):
        (task, count) = data
        self.task = trace.tasks[task]
        self.count = count

class RuleScanningDeferredOnInput(Event):
    def __init__(self, trace, data):
        (rule, inputRule) = data
        self.rule = trace.rules[rule]
        self.inputRule = trace.rules[inputRule]

class RuleScanningDeferredOnTask(Event):
    def __init__(self, trace, data):
        (rule, inputTask) = data
        self.rule = trace.rules[rule]
        self.inputTask = trace.tasks[inputTask]

class RuleScanningNextInput(Event):
    def __init__(self, trace, data):
        (rule, inputRule) = data
        self.rule = trace.rules[rule]
        self.inputRule = trace.rules[inputRule]

class UnblockedTask(Event):
    def __init__(self, trace, data):
        (task,) = data
        self.task = trace.tasks[task]

class ReadiedTask(Event):
    def __init__(self, trace, data):
        (task, rule) = data
        self.task = trace.tasks[task]
        assert(self.task.rule is trace.rules[rule])

class FinishedTask(Event):
    def __init__(self, trace, data):
        (task, rule, effect) = data
        self.task = trace.tasks[task]
        assert(self.task.rule is trace.rules[rule])
        self.effect = effect

def _create_rule(trace, data):
    rule = Rule(data)
    trace.rules[rule.name] = rule

def _create_task(trace, data):
    task = Task(data)
    trace.tasks[task.name] = task
             
_event_handlers = {
    "new-rule": _create_rule,
    "new-task": _create_task,

    "build-started": BuildStarted,
    "build-ended": BuildEnded,
    "handling-build-input-request": HandlingBuildInputRequest,
    "checking-rule-needs-to-run": CheckingRuleNeedsToRun,
    "rule-needs-to-run": RuleNeedsToRun,
    "rule-does-not-need-to-run": RuleDoesNotNeedToRun,
    "created-task-for-rule": CreatedTaskForRule,
    "handling-task-input-request": HandlingTaskInputRequest,
    "added-rule-pending-task": AddedRulePendingTask,
    "rule-scheduled-for-scanning": RuleScheduledForScanning,
    "paused-input-request-for-rule-scan": RuleScheduledForScanning,
    "readying-task-input-request": ReadyingTaskInputRequest,
    "completed-task-input-request": CompletedTaskInputRequest,
    "updated-task-wait-count": UpdatedTaskWaitCount,
    "rule-scanning-deferred-on-input": RuleScanningDeferredOnInput,
    "rule-scanning-deferred-on-task": RuleScanningDeferredOnTask,
    "rule-scanning-next-input": RuleScanningNextInput,
    "unblocked-task": UnblockedTask,
    "readied-task": ReadiedTask,
    "finished-task": FinishedTask,
}

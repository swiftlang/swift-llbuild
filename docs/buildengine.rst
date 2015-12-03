==============
 Build Engine
==============

This document currently only provides additional information on some aspects of
the build engine design. See the source code documentation comments for
information on the actual ``BuildEngine`` APIs.

Order-only Dependencies
=======================

The build engine supports "order-only" dependencies (in the style of Ninja)
through a "must-follow" input request or barrier. Internally, this is treated
much the same as a normal input request, in that the task is responsible for
calling ``taskMustFollow()`` on the engine during its preparation phase.

However, these input requests do not need to be scanned or persisted. This is
because the dependency is only relevant when the dependee is executing, and in
those cases the task will be responsible for dynamically supplying the
barrier. Thus, the implementation can piggy-back on the existing mechanisms for
ensuring input requests are available prior to executing the task, and simply
discard the order-only input request once satisfied.

Parallelism
===========

There are two obvious approaches to introducing parallelism into the core build
engine.

1. The first approach (which is taken currently) changes the task definition so
   that it gets a callback ``inputsAvailable()``, and adds a new task API on the
   ``BuildEngine`` called ``taskIsComplete()`` which is allowed to be called
   concurrently.

   When the task gets all of its inputs, then it is responsible for dispatching
   its work in a parallel fashion and informing the engine when that is
   complete. The engine tracks the task as pending completion until it received
   that callback.

   This keeps the engine simple, as it doesn't really deal with concurrency at
   all except for in specific cases that should be easy to reason about. It also
   makes the engine separable from the specific concurrency model, which may
   make it easier to plug in schedulers and the like (for example, it is
   somewhat clear how a posix_spawn + ppoll/pselect concurrency model works in
   this system).

2. The alternate approach is to change the engine so that it manages all of the
   concurrency, changing the Task contract such that it stays synchronous but
   can be called on any thread.

   This means that all clients of the engine get concurrency automatically, but
   also more intimately ties the engine implementation to the concurrency model.


Dependency Scanning
===================

The engine is designed to scan dependencies concurrently with other build
work. This introduces a fair amount of complexity into the engine, but is
important because we want to ensure that the engine is able to dispatch any
identified tasks as soon they are found.

Recursive Dependency Scanning
-----------------------------

The current design of the engine handles dependency scanning in the same manner
as other tasks, it associates a scan request with a rule and then enqueues the
actual scanning work so that it can proceed in parallel with other work.

Currently, the dependency scanning processes each input for an individual rule
in sequence, and it suspends itself when it is waiting on an individual input to
complete. This is made more complex by the need to sometimes execute a task
before being able to determine that its downstream clients need to rerun (when
the result has not changed). To manage that, the engine tracks for each task
which other tasks are waiting for it to complete as well as which tasks are
waiting for it to be scanned.

The current implementation is not yet capable of scanning multiple inputs for a
particular task concurrently, because when doing it is necessary to track
additional state about whether or not the input was *actually* requested by a
task or just a dependency present on a previous iteration. For example, consider
the case where a task depended upon **A** and **B** in the last build. If a
concurrent scan indicates the task should rerun because **B** has changed, it is
not necessarily true that **B** itself should rerun (because the task may no
longer request **B** as an input). The current implementation side-steps this
problem by scanning dependencies one at a time.

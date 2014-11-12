==============
 Build Engine
==============

Parallelism
===========

There are two obvious approachs to introducing parallelism into the core build engine.

1. The first approach (which is taken currently) changes the task definition so
   that it gets a callback like ``inputsAreReady()``, and add a new task API on
   the ``BuildEngine`` such as ``taskIsComplete()`` which is allowed to be
   called concurrently.

   When the task gets all of its inputs, then it is responsible for dispatching
   its work in a parallel fashion and informing the engine when that is
   complete. The engine would track the task as pending completion until it
   received that callback.

   This keeps the engine simple, as it doesn't really deal with concurrency at
   all except for in specific cases that should be easy to reason about. It also
   makes the engine separable from the specific concurrency model, which might
   make it easier to plug in schedulers and the like (for example, it is
   somewhat clear how a posix_spawn + ppoll/pselect concurrency model works in
   this system).

2. The alternate approach is to change the engine so that it manages all of the
   concurrency, changing the Task contracting such that it stays synchronous but
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

The current design of the engine handles dependency scanning as in the same
manner as other tasks, it assocates a scan request with a rule and then enqueues
the actual scanning work so that it can proceed in parallel with other work.

Currently, the dependency scanning process each input for an individual rule in
sequence, and it suspends itself when it is waiting on an individual input to
complete. The alternative would be to scan all inputs concurrently, and allow
the rule to transition out of the scanning state as soon as the any input would
trigger it to do so.

The current approach is somewhat simpler than the alternative, and in my
experiements it has always yielded a faster overall dependency scanning
algorithm. However, it has the downside that the engine may not discover work
that needs to be done as soon as possible, which could increase latency and thus
reduce parallelism. I would like to revisit this design decision once more real
world workloads are available.

The alternative approach can be implemented in a fairly elegant fashion -- we
introduce an additional queue of individual input scan requests to process. When
we scan the rule initially, any request that needs to be deferred just defers
the scan of that individual input. This drastically simplifies the
``RuleScanRequest`` and ``DeferredScanRequests`` data structures, which both
become just a ``RuleInfo*``.

The ``misc/patches`` directory currently has two patches that demonstrate an
implementation of the alternative approach, but both of them degrade the overall
dependency scanning performance severely.

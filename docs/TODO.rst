======
 TODO
======

General
=======

* Import LLVM Support and ADT.

Ninja Manifests
===============

* Diagnose multiple build decls with same output.

Build Engine
============

* Generalize key type.

* Generalize value type.

* Support multiple outputs.

* Support active scheduling (?).

* Figure out when taskNeedsInput() should be allowed, and if
  taskDiscoveredDependency() should be eliminated in favor of loosened rules
  about when it can be invoked.

* Figure out if Rule should be subclassed, with virtual methods for action
  generation and input checking.

* Implement proper error handling.

* Think about how to implement dynamic command failure -- how should this be
  communicated downstream? This is another area where flexibility might be nice.

* Investigate how Ninja handles syncing the build graph with the on disk state
  when it has no DB (nevermind, seems to rebuild). What happens when an output
  of a compile command is touched?

* Introspection features for watching build status.

* Performance

  * Think about # of allocations in engine loop.

  * Think about whether there is any way to get of per-task variable length
    list.

  * Think about making-progress policy, what order do we prefer starting tasks
    vs. providing inputs vs. finishing tasks.

  * Should we finalize immediately instead of moving things around on queues.

  * We need to avoid the duplicate stating we currently do of input files.

  * Implement an efficient shared queue for FinishedTaskInfos, if we stay with
    the current design.

  * Figure out if we should change the Rule interface so that the engine can
    manage a free-list of allocated Tasks.

Build Database
==============

* Performance

  * Should we support DB stashing extra information in Rule (its own ID
    number). Should the engine officially maintain the ID (maybe useful once we
    generalize anyway).

  * Should we support the DB doing a bulk load of the rule data? Probably more
    efficient for most databases, but on the other hand this would go against
    moving to a model where the data is *only* stored in the DB layer, and the
    BuildEngine always has to go to the DB for it. That model might be slower,
    but more scalable.

Ninja Specific
==============

Tasks for Usable Tool
---------------------

* Don't run commands downstream of failing ones.

* Rework command line tool towards production use vs testing tool.

  * This mean factoring out a separate llbuild-test testing tool which has the
    interface that is useful for testing, with none of the other stuff.

  * Automatically set up appropriate jobs, DB, etc.

* Support multiple inputs on the command line.

* Support finding root targets when no default is set.

* Buffer command output.

* Support "restat" feature.

* Implement path normalization (for Nodes as well as things like imported
  dependencies from compiler output).

* Implement support for automatic running of the rule that generates the
  manifest itself (rerun on CMake changes). Also, look into related problems:

  * If CMake is killed while running, then llbuild will never run it again (even
    if the CMakeLists.txt files are touched). This is actually just a concrete
    instance of our output file handling behavior not being finished, as the
    generator file has phony edges on all of the CMake inputs.

* Implement support for rerunning commands which have changed.

Random Tasks
------------

* Investigate using pselect mechanisms vs blocked threads.

* Support proper handling of missing outputs (and outdated)?.

* Performance

  * Need to optimize the evalString() stuff. A good performance test for this is
    to compare ``ninja -n`` on the chromium fake build.ninja file vs ``llb ninja
    build --quiet --no-execute`` (62% of which is currently the loading, and 43%
    of which is evalString()).

  * It would be super cool to use the build engine itself for managing the
    loading of the .ninja file, if we cached the result in a compact binary
    representation (the build engine would then be used to recompute that as
    necessary). This would be a nice validation of the generic engine approach
    too.

======
 TODO
======

Generic
=======

* It would be nice to have some basic infrastructure for handling command line
  parsing better.

* It would be nice to have some infrastructure for statistics (a la LLVM).

Ninja Manifests
===============

* Diagnose multiple build decls with same output.

Build Engine
============

* Generalize key type.

* Generalize value type (cleanly, current solution is gross).

* Support multiple outputs.

* Support active scheduling (?).

* Consider moving the task model to one where the build engine just invokes
  callbacks on the delegate (probably using task IDs, and maybe kind IDs for the
  use of clients which want to follow class-based dispatch models). This has two
  advantages, (1) it maps more neatly to an obvious C API, and (2) it should
  make it easier to cleanly generalize the value type because only the engine
  and delegate need to be templated, and neither support subclassing.

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

  * Investigate using DB entirely on demand, per above comment. Very scalable.

  * Normalize key and dependency node types to a unique entry to reduce
    duplication of large key values.

  * Many clients end up having additional information about a key that then gets
    lost when they serialize it and get it back somewhere else (e.g., one task
    requests the key, another starts the key). For example, the client most
    likely has some internal state associated with that key that would be really
    nice to be able to pass around.

    We can solve this by making the key type richer, and allowing it to have
    serialization as just one of its methods. We could use a virtual interface
    or just a simple mechanism to attach a payload.


Ninja Specific
==============

Tasks for Usable Tool
---------------------

* Implement path normalization (for Nodes as well as things like imported
  dependencies from compiler output).

* Implement Ninja failure semantics for order-only dependencies, which block the
  downstream command.

* Handle removed implicit dependencies properly, currently this generates an
  error and then builds ok on the next iteration. The latter problem may
  indicate a latent issue in handling of discovered dependencies.

* Handle rerunning a command on introduction of new dependencies. For example,
  before we reran on command changes we wouldn't rebuild a library just because
  it depended on a new file. We should probably make sure this happens even if
  the command doesn't change, although it might be good to check vs Ninja.

* Support update-if-newer for commands with discovered dependencies.

* We should probably store a rule type along with the result, and always
  invalidate if the rule type has changed.

Random Tasks
------------

* Ninja reruns a command if the restat= flag changes? What triggers this? This
  behavior probably also happens for depfiles and things, we should match.

* There are some subtle differences in how we handle restat = 0, and generally
  we don't do the same thing Ninja would (we can rebuild less). This is because
  Ninja will just run things downstream if an incoming edge was dirty, but we
  will do so and also allow the update-if-newer behavior to trigger on interior
  commands. As an example, look at how the multiple-outputs test case behaves in
  Ninja with restat = 0.

* Tasks should have a way to examine their discovered dependencies from a
  previous run. For example, this is necessary to implement update-if-newer for
  commands with discovered dependencies.

* Add support for cleaning up output files (deleting them on failed tasks)?

* Investigate using pselect mechanisms vs blocked threads.

* Support traditional style handling of updated outputs (in which consumer
  commands are rerun but not the producer itself).

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

  * There is some bad-smelling redundancy between how we check the file info in
    the `*IsValid()` functions, and how we then recompute that info later as part
    of the task (and the engine internally will compare it to see if it has
    changed to know if it needs to propagate the change). We need to think about
    this and figure out what is ideal. There might be a cleaner modeling where
    we discretely represent each stat-of-file as an input that is then consumed
    by each item that requires it. This would make it easy to guarantee we
    compute such things once.

  * I have heard a claim that one can actually improve performance by
    strategically purging the OS buffer cache -- the claim was that it is faster
    to build Swift after building LLVM & Clang if there is a purge in
    between. If true, this may be better things we can do to communicate to the
    kernel the purpose and lifetime of things like object files.

  * We should consider allowing the write of the target result to go directly
    into the stored Result field. That would avoid the need for spurious
    allocations when updating results.

  * We need to switch the Rule Dependencies to be stored using the ID of the
    rule (which means we need to assign rule IDs, but the DB would like that
    anyway). This dramatically reduces the storage required by the database
    (although a lot of that is because of our subpar phony command
    implementation, and it would drop significantly if we switch to a
    specialized implementation for phony commands, because we don't need the
    clunky giant composite-key).

  * We should use a custom task for Phony commands, they have a lot of special
    cases (like the one above about the composite key size).

  * We should move to a compact encoding for the build value. Not worth doing
    until we address the rule_dependencies table size.


Build System
============

Build File
----------

 * We will probably want some way to define properties shared by groups of tasks
   (for example, common flags), for efficiencies sake. There are a couple ways
   to do this:

   * We could make the build file an "immediate-mode" sort of interface, and
     allow interleaving of tool and task maps. Then the client could just
     generate the file with updated information interleaved. This would be
     similar to how Ninja files get generated in practice by nice generators
     (`gyp`, not `CMake`).

   * We could allow the definition of tool aliases, that can define additional
     properties. This lets the format be better definite and not have immediate
     mode stateful problems.

 * We want some way to allow the task name and one of the tasks outputs to be
   the same, without having a redundant specification.

 * We might need a mechanism for defining default properties for nodes.

 * We may want to add a notion of types for nodes. We could try and be context
   dependent too, but having a type here would make it easier for the client to
   bind the node to the right type during loading.

 * We may want some provision for providing inline node attributes with the task
   definitions. Otherwise we cannot really stream the file to the build system
   in cases where node attributes are required.

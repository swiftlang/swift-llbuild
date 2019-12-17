# Build System Internals

This document is complementary to the documentation on the external
``BuildSystem`` interface, and describes how the build system internally maps
the client facing model to the underlying ``Core`` build engine.

## Overview

Internally, the build system models all of its keys using the `BuildKey` type
which is a union over all possible keys, and which implements a simple binary
encoding to and from the raw data used by the low-level build engine.

Similarly, all of the values which can be produced are modelled as via
`BuildValue` type which is a similar union with binary coding support.

Even though the input graph is bipartite, we model all keys and values in the
same namespace to simplify encoding issues, and because the low-level build
engine does not yet provide any facilities for enforcing the type safety of the
keys and values with regard to changes in the key space (for example, should a
build node's name ever change to become that of a build command, the engine has
no facility to automatically invalidate any prior `BuildValue` associated with
that key).


## Directory Handling

Directory handling by the build system component is split across several
coordinating tasks:

### Directory Contents 

The directory contents task transforms `DirectoryContents` key requests into a
list of the filenames contained within that directory (if present, and a
directory) as well as the ``FileInfo`` for the directory itself which is used as
a proxy to validate its contents on incremental rebuilds.

### Directory Tree Signature

The directory tree signature task is responsible for computing the signature for
an entire recursive directory tree structure. It does so incrementally and
dynamically by recursively requesting the information for each subpath it
discovers, as follows:

1. The contents of the input path are requested.

2. The build node for each path contained within the directory is requested,
   based on the information in #1.
   
   An important side effect of this is that it establishes a strong dependency
   between the signature and any producers of the nodes contained in the
   directory, thus effectively dynamically discovering the commands which
   contribute content to the directory.
   
   ```eval_rst
   .. note::
   
     NOTE: The signature task does *NOT* attempt to perform a global scan of the
     dependency graph to find nodes which **are not** currently present in the
     directory, but which **are** the output of some task. The client is
     currently responsible for ensuring that any commands which produce output
     in the directory should be strongly ordered before the signature node.

     However, clients can explicitly make additional dependencies on the
     directory tree by registering a phony command which produces the directory
     used by the tree signature, and which adds input dependencies on other
     nodes in the graph (for example, ones which may or may not produce content
     within the directory tree, and thus must be run before it).
   ```

3. Recursively request directory signatures for any directories discovered as
   part of #2.
   
The task accumulates the results from all of the dependency requests made as
part of #2 and #3, and stores them in a deterministic order (based on the sorted
names of the directory subpaths).

Finally, the signature is computed as a hash of all of the accumulated
values. Since the raw encoded value is deterministic, stable, and a complete
representation of the node, we simply compute the aggregate hash for the all of
the encoded results.


## Known Issues

The entire `BuildSystem` layer is now regarded as something of a design mistake
in llbuild (at least by https://github.com/ddunbar). The `BuildSystem` layer was
built as an encapsulation *over* the `Core` layer, but in hindsight what most
likely should be done instead is that the `BuildSystem` component is an adjacent
component to `Core` that exposes a lot of useful "utility" functionality for
building traditional "build systems" on top of the `Core`, but it should do so
without hiding the actual `Core` API.

In the current situation, what has effectively happened is that the
`BuildSystem` layer has just created an extra bridge layer on top of the `Core`,
but with sometimes different naming or slightly different API features, and then
even worse the `BuildSystem` C API exposes an even more restricted/specialized
view. This makes the code base and execution very confusing because to truly
understand what a high-level build system on top is doing, one ends up tracing
through several levels with similar functionality but different terms, and it is
very easy to get lost. It also makes it very hard to evolve, because it is
difficult to see at exactly which layer a change should be made, and sometimes
it requires substantially boilerplate propagation through multiple API levels.

The motivation for this layout was to try and offer a high-performance C++
library that could handle several of the usability / mechanical / performance
sensitive tasks that a build system wants, for example having some kind of
serialized manifest and command line tooling that can leverage that. While the
library met this need to some extent, it hasn't worked very well in
practice. This could either be an artifact of the API not being right, or it
could be that the basic tenants are wrong, and instead the library tries to
combine two many orthogonal concerns into one.

To summarize some of the problems we are aware of:

* The abstractions are confusing, even for core developers.
* Clients are forced to create a manifest, rather than in memory tasks.
* Publicly bound abstractions (e.g. in Swift) are very limited, don't expose
  `Core` power.

### Existing Abstractions

Below is a summary of the existing abstractions above `Core`, and their purpose:

* Public:
  * BuildSystem :: BuildSystem
  * BuildSystem :: BuildSystemFrontend
  * BuildSystem :: BuildKey
  * BuildSystem :: BuildValue
  * BuildSystem :: Target
  * BuildSystem :: Node
  * BuildSystem :: BuildNode
  * BuildSystem :: Tool
  * BuildSystem :: Command
  * BuildSystem :: ExternalCommand
  * BuildSystem :: ShellCommand
  * BuildSystem :: BuildSystemCommandInterface
  * BuildSystem C API :: CAPITool
  * BuildSystem C API :: CAPIExternalCommand
  * BuildSystem Swift API :: ExternalCommand
  * BuildSystem Swift API :: Command (???)
    * This one is very very confusing, despite the same name as BuildSystem ::
      Command it is a fundamentally different thing, I believe an
      ExternalCommand wraps a Command.
  * BuildSystem Swift API :: Tool

### Future Directions

We (https://github.com/ddunbar, https://github.com/dmbryson,
https://github.com/sergiocampama) have several concrete ideas about how to make
forward progress on untangling this rather messy situation:

* Develop some kind of new command or binding in the BuildSystem layer that is a
  much more direct exposure of the underlying Core API.
  
  This command should get a high-parity C & Swift API exposure, so that it is
  possible to write highly flexible tasks/commands in `BuildSystem` clients
  (currently this can only be done *internal* to the library.
  
* Refactor key utility code in the BuildSystem library so that is more
  composable, i.e., so it is available to clients of the BuildSystem library.
  
  * For example, we should make sure that clients can leverage llbuild's
    subprocess launching code rather than need to use their own.
    
* Use the above two pieces to rewrite the `BuildSystem`'s existing built-in
  tasks in terms of the new APIs. Ideally, we would at the same time move them
  out of C++ into Swift so they are easier to modify and help inform Swift-based
  clients.

* As the above move, eliminate unnecessary `BuildSystem` internal abstractions
  and APIs.

* Provide new extension points so that clients are able to "hook" more of the
  internal process. For example, clients cannot currently hook the command
  lookup process, so they are forced to put all commands into the `BuildSystem`
  manifest, even though this can be redundant (especially in cases where a
  complex client like Xcode's build system is maintaining is own sideband build
  description file).
  
  * This is similar in one tiny way to the existing `CustomCommand`
    micro-abstraction, which is actually only used in one single place. We
    should definitely ensure that anything new replaces (or extends) this.

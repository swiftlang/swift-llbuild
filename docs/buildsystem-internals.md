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

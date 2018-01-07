==============
 Build System
==============

This document describes the *llbuild*-native ``BuildSystem`` component. While
*llbuild* contains a Core engine which can be used to build a variety of build
systems (and other incremental, persistent, and parallel computational systems),
it also contains a native build system implementation on which higher level
facilities are built.


Design Goals
============

As with the core engine, the ``BuildSystem`` is designed to serve as a building
block for constructing higher level build systems.

The goal of the ``BuildSystem`` component is that it should impose enough policy
and requirements so that the process of executing a typical software build is
*primarily* managed by code within the system itself, and that the system has
enough information to be able to perform sophisticated tasks like automatic
distributed compilation or making use of a in-process compiler design.


Build Description
=================

Conceptually, the ``BuildSystem`` is organized around a bipartite graph of
commands and nodes. A ``Command`` represent a units of computation that need to
be done as part of the build, and a ``Node`` represent a concrete value used as
an input or output of those commands. Each ``Command`` declares which ``Tool``
is to be used to create it. The tools themselves are integrated into the
``BuildSystem`` component (either as builtin tools, or via the client), and this
allows them to provided customizable features for use in the build file, and to
be deeply integrated with *llbuild*.

The build graph is supplied to the ``BuildSystem`` via a ``BuildDescription``
which can either be loaded from a build file (see below), or can be directly
constructed by clients. The latter facility is currently only used for
constructing unit tests, and is not exposed via the public llbuild API.

Nodes
-----

Each node represents an individual value that can be produced as part of the
build. In a file based context, each node would conceptually represent a file on
the file system, but there is no built-in requirement that a node be synonymous
with a file (that said, it is a common case and the ``BuildSystem`` will have
special support to ensure that using nodes which are proxies for files on disk
is convenient and featureful).

There are several different types of nodes with special behavior. The build
system allows controlling these behaviors on a per node basis, but in order to
keep build files succinct, there is also support for inferring the type of a
node from naming conventions on the node itself.

These are the supported node types:

* File Nodes: A node is by default assumed to represent a path in the filesystem
  with the same name as the node.

* Directory Tree Nodes: A node ending with "/" is assumed to represent a
  *directory tree*, not an individual file. The node's value will be a signature
  of the recursive contents of the entire directory tree at that path, and any
  changes to any part of the directory structure will causes commands taking it
  as an input to rerun.

  For example, in the following build file fragment ``C1`` uses a directory node
  because the task is doing a recursive copy of the input directory::
  
      commands:
        C1:
          tool: shell
          inputs: ["input/"]
          outputs: ["output/"]
          args: rm -rf output && cp -r input output

  .. note::
    It is legal to use a directory tree node to refer to a path which is
    *actually* just a file; the node will be considered as changed whenever the
    file on disk is changed. This is useful when the client cannot easily know
    in advance whether the node is expected to be a file or a directory, but
    should be treated as a directory tree if it is one.

  The directory tree will eagerly scan the directory as soon as any commands
  which produce the immediate directory are complete. This means that the build
  graph **MUST** contain a complete dependency graph between the tree node and
  any command which may produce content within the directory. If such a
  dependency is missing, the build system will typically end up scanning the
  directory before all content is produced, and this will result in the first
  build being incomplete, and the next build redoing the remainder of the work.

* Directory Tree Structure Nodes: These are like directory tree nodes, but
  instead of tracking all attributes of the directory, they will change only
  when the *structure* of the directory (recursively) changes. That is, they
  will not rerun when the contents of files in the directory are modified, only
  when files are added or removed, or files change type.

  This is useful for clients which infer properties based purely on a directory
  structure. Such clients can use this node type to track when to redo that
  computation, and use additional dependencies on particular files for any items
  within the structure whose content are relevant to the task.
  
* Virtual Nodes: Nodes matching the name ``'<.*>'``, e.g. ``<gate-task>``, are
  *assumed* to be virtual nodes, and are used for adding arbitrary edges to the
  graph. Virtual nodes carry no value and only will only cause commands to
  rebuild based on their presence or absence. see the documentation of the
  ``is-virtual`` node attribute for more information.

* Command Timestamps: A node can be marked as being a *command timestamp* (see
  the ``is-command-timestamp`` node attributes). Command timestamps are always
  virtual, but will carry a value representing the time at which the command
  which produces them was run. When used as an input to a subsequent command,
  this will cause that command to rerun whenever the producer of the timestamp
  is run. This can be used to build triggers such that one command will always
  force another to build.

Commands
--------

Each ``Command`` is used to represent the things that do actual work in the
build graph, and they take input nodes and transform them into output ones. A
command could be implemented as something which invokes an external command
(like `cc`) to do work, or it could be something which is implemented internally
to the ``BuildSystem`` or the client (for example, a command to compute the
SHA-1 hash of a file).

A ``Command`` as modeled in the ``BuildSystem`` component is related to, but
different, from the tasks that are present at the lower ``BuildEngine`` layer. A
``Command`` in the ``BuildSystem`` is roughly equivalent to a
``BuildEngine::Rule`` and the ``BuildEngine::Task`` which computes that rule,
but in practice a ``Command`` in the ``BuildSystem`` might be implemented using
any number of coordinating rules and tasks from the lower level. For example, a
compiler ``Command`` in the ``BuildSystem`` might end up with a task to invoke
the compiler command, a task to parse the diagnostics output, and another task
to parse the header dependencies output.

Every ``Command`` has a unique identifier which can be used to refer to the
command, and which is expected to be stable even as the build graph is updated
by the generating tool.

Tools
-----

A ``Tool`` defines how a specific command is executed, and are the general
mechanism through which the ``BuildSystem`` can support various styles of work
(as opposed to just running commands) and extension by clients.

Every ``Command`` has an associated tool which defines how it will be run, and
can provide additional tool-specific properties to control its execution. For
example, a ``Command`` which invokes a generic tool that runs external commands
would typically provide the list of command line arguments to use. On the other
hand, a ``Command`` which uses a higher-level tool to invoke the compiler may
set additional properties requesting that automatic header dependencies be used.


Build File
==========

The build file is the base input to the native build system, similar to a
Makefile or a Ninja manifest. It contains a description of the things that can
be built, the commands that need to be executed to build them, and the
connections between those commands. Similar to Ninja, the basic build file
language is not intended to be written directly, but is expected to be an
artifact produced by the higher level build system.

The build file syntax is currently YAML, to faciliate ease of implementation and
evolution. At some future point, we may wish to change to a custom file format
to optimize for the native build system's specific requirements (in particular,
to reduce the file size).

A small example build file is below:

.. code-block:: yaml
  
  # Declare the client information.
  client:
    name: example-client
    version: 1

  # Define the tools.
  tools:
    cc:
      enable-dependencies: True
      cwd: /tmp/example
    link:
      cwd: /tmp/example
  
  # Define the targets.
  targets:
    hello: ["hello"]
  
  # Define the default target to execute.
  default: hello
  
  # Define properties on nodes.
  nodes:
    hello.o:
      hash-content: True
    
  # Define the commands.
  commands:
    link-hello:
      tool: link
      inputs: ["hello.o"]
      outputs: ["hello"]
    cc-hello.o:
      tool: cc
      input: ["hello.c"]
      outputs: ["hello.o"]
      args: -O0

The build file is logically organized into five different sections (grouped by
keys in a YAML mapping). These sections *MUST* appear in the following order if
present.

* Client Definition (`client` key)

  Since the BuildFile format is intended to be reused by all clients of the
  ``BuildSystem`` component, the client section is used to provide information
  to identify exactly which client should be used to build this build file. The
  section gives the name of the client, and an additional version that can be
  used by the client to version semantic changes in the client hooks.

  The name field is required, and must be non-empty.

  The version field is optional, and defaults to 0.

  Additional string keys and values may be specified here, and are passed to the
  client to handle.

* ``Tool`` Definitions (`tools` key)

  This section is used to configure common properties on any of the tools used
  by the build file. Exactly what properties are available depends on the tool
  being used.

  Each property is expected to be a string key and a string value.

* Target Definitions (`targets` key)

  This section defines top-level targets which can be used to group commands
  which should be build together for a particular purpose. This typically would
  include definitions for all of the things a user might want to build directly.
  
* ``Default`` Definitions (`default` key)

  This section defines the default target to build when manifest is loaded.
  
* ``Node`` Definitions (`nodes` key)

  This section can be used to configure additional properties on the node
  objects. ``Node`` objects are automatically created whenever they appear as an
  input or output, and the properties of the object will be inferred from the
  context (i.e., by the command that produces or consumes them). However, this
  section allows customizing those properties or adding additional ones.

  Each key must be a scalar string naming identifying the node, and the value
  should be a map containing properties for the node.

  Each property is expected to be a string key and a string value.

  .. note::
    FIXME: We may want to add a mechanism for defining default properties.

  .. note::
    FIXME: We may want to add the notion of types to nodes (for example, file
    versus string).

* ``Command`` Definitions (`commands` key)

  This section defines all of the commands as a YAML mapping, where each key is
  the name of the command and the value is the command definition. The only
  required field is the `tool` key to specify which tool produces the command.

  The `tool` key must always be the leading key in the mapping.

  The `description` key is available to all tools, and should be a string
  describing the command.
  
  The `inputs` and `outputs` keys are shared by all tools (although not all
  tools may use them) and are lists naming the input and output nodes of the
  ``Command``. It is legal to use undeclared nodes in a command definition --
  they will be automatically created.

  All other keys are ``Tool`` specific. Most tool specific properties can also
  be declared in the tool definitions section to set a default for all commands
  in the file, although this is at the discretion of the individual tool.

  .. note::
    FIXME: We may want some provision for providing inline node attributes with
    the command definitions. Otherwise we cannot really stream the file to the
    build system in cases where node attributes are required.

Format Details
--------------

The embedding of the build file format in YAML makes use of the built in YAML
types for most structures, and should be self explanatory for the most
part. There are two important details that are worth calling out:

1. In order to support easy specification of command lines, some tools may allow
   specifying command line arguments as a single string instead of a YAML list
   of arguments. In such cases, the string will be quoted following basic shell
   syntax.

.. note::
  FIXME: Define the exact supporting shell quoting rules.

2. The build file specification is designed to be able to make use of a
   streaming YAML parser, to be able to begin building before the entire file
   has been read. To this end, it is recommended that the commands be laid out
   starting with the commands that define root nodes (nodes appearing in
   targets) and then proceeding in depth first order along their dependencies.

Dynamic Content
---------------

.. note::
  FIXME: Add design for how dynamically generated work is embedded in the build
  file.


Node Attributes
===============

As with commands, nodes can also have attributes which configured their
behavior.

The following attributes are currently supported:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Name
     - Description

   * - is-directory
   
     - A boolean value, indicating whether or not the node should represent a
       directory instead of a file path. By default, the build system assumes
       that nodes matching the pattern ``'.*/'`` (e.g., ``/tmp/``) are directory
       nodes. This attribute can be used to override that default.

   * - is-directory-structure
     - A boolean value, indicating whether the node should represent the
       directory structure of a file path. Such nodes should be name as
       '<path>/' (which would normally be a directory node), and then this
       attributed used to change the type.

   * - is-virtual
     - A boolean value, indicating whether or not the node is "virtual". By
       default, the build system assumes that nodes matching the pattern
       ``'<.*>'`` (e.g., ``<link>``) are virtual, and all other nodes correspond
       to files in the file system matching the name. This attribute can be used
       to override that default.

   * - is-command-timestamp
     - A boolean value, indicating whether the node should be used to represent
       the "timestamp" at which a command was run. When set, the node should
       also be the output of some command in the graph. Whenever that command is
       run, the node will take on a value representing the timestamp at which
       the command was run.

       This node can then be used as a (virtual) input to another command in
       order to cause the downstream command to rerun whenever the producing
       command is run.

       Such nodes are always virtual nodes.

   * - is-mutated
     - A boolean value, indicating whether the node is mutated by commands in
       the build. When a command is mutated, it's file system information will
       no longer be used in determining whether a detected change in the
       *output* of a command should cause that command to rerun. Without this
       check, the producer of the file would always rerun since the output
       information captured at production time will always be out-of-date once
       the mutating command runs.
       
.. note::
  FIXME: At some point, we probably want to support custom node types.


Builtin Tools
=============

The build system provides several built-in tool definitions which are available
regardless of the client.

The following tools are currently built in.

Phony Tool
----------

**Identifier**: *phony*

A dummy tool, used for imposing ordering and grouping between input and output
nodes.

No attributes are supported other than the common keys.

Mkdir Tool
----------

**Identifier**: *mkdir*

This tool is used to recursively create directories, with appropriate dependency
tracking. This tool should be used when clients only care about the existence of
the directory, not any other aspects of it. In particular, it ignores changes to
the directory timestamp when consider whether to run.

No attributes are supported other than the common keys. The sole output should
be the node for the path to create. Arbitrary inputs can be declared, but they
will only be used to establish the order in which the command is run.

Symlink Tool
------------

**Identifier**: *symlink*

This tool is used to create a symbolic link at a particular location, with
appropriate dependency tracking. Due to the nature of symbolic links it is
important to use this tool when creating links during a build, as opposed to the
usuall `shell` tool. The reason why is that the build system will, by default,
use `stat(2)` to examine the contents of output files for the purposes of
evaluating the build state. In the case of a symbolic link this is incorrect, as
it will retrieve the status information of the target, not the link itself. This
may lead to unnecessary recreation of the link (and triggering of subsequent
work).

The sole output should be the node for the path to create. Arbitrary inputs can
be declared, but they will only be used to establish the order in which the
command is run.

.. note::

   The issue here may be encountered by any other tool which needs to create
   symbolic links during the build. We do not yet expose this as a general
   purpose feature available to any command, but that may be a desirable feature
   in the future.

.. note::

   The defined output of this tool will be the file system information on the
   **link**, not the target of the link. This is almost always **not** what
   clients want unless also using *link-output-path*, since many consumers of
   the output will want to know about the **target** of the link.
   
.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Name
     - Description

   * - contents
     - The contents (i.e., path to the source) of the symlink.

   * - link-output-path
     
     - If specified, defines that actual output path for the symbolic link. This
       is **not** treated as a declared output of this task, which allows a
       *phony* task to be created which will then define the modeled value for
       this path. This allows a client to create a build in which both the
       `lstat()` and `stat()` information for a link are accurately modeled.

Shell Tool
----------

**Identifier**: *shell*

A tool used to invoke shell commands. This tool only supports defining
attributes on commands, and not at the tool level.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Name
     - Description

   * - args
     - A string or string list indicating the command line to be executed. If a
       single string is provided, it will be executed using ``/bin/sh -c``.

   * - env
     - A mapping of keys and values defining the environment to pass to the
       launched process. See also `inherit-env`.

   * - inherit-env
     - A boolean flag controlling whether this command should inherit the base
       environment provided when executing the build system (either from the
       command line, or via the internal C APIs), or whether it should only
       include the entries explicitly provided in the `env` mapping above.

   * - allow-missing-inputs
     - A boolean value, indicating whether the commands should be allowed to run
       even if it has missing input files. The default is false.

   * - allow-modified-outputs
     - A boolean value, indicating whether the a command's outputs are allowed
       to be modified independently from the command without invalidating the
       result. The default is false.

       This can be useful when it is necessary to define builds in which one
       command modifies the state of another command (e.g., a common example is
       running something like a `strip` tool directly on the output of a link
       step).

       The command will be rerun if the outputs are missing, but will not
       otherwise rerun the command if the output has only changed state.

       .. note::

          This is an experimental feature; commands downstream of outputs
          produced by such a tool will inherit the behavior that they do not
          re-run if the output is only mutated (not recreated).

   * - always-out-of-date
     - A boolean value, indicating whether the commands should be treated as
       being always out-of-date. The default is false.
          
   * - deps
     - The path to an output file of the command which will contain information
       on the exact dependencies used by the command when it ran. This can be
       used as a way to avoid the need to specify all dependencies up-front, in
       particular for use in situations like compiling C source code where it is
       hard to predict the exact set of headers which may be needed in advance.

       This mechanism works based on the following observations:

       * If a command has never run before, it will always need to be run, so it
         is often safe to not know the complete set of dependencies up front.

       * Once the command has run, if it tells us the exact set of dependencies
         it used then we can end up with precise information on the required
         dependencies, in order to rebuild it correctly in the future.

       Note that these observations are only true **if** all of the needed
       dependencies are already present. If those dependencies are themselves
       computed by some other task in the build system (e.g., a generated
       header) then the client is responsible for making sure that those inputs
       will have been produced first.

       The exact format of the output file is specified via the separate
       `deps-style` key.

       This option also supports being passed multiple output file paths, for
       clients where it is more convenient to produce several distinct
       dependencies output files.

   * - deps-style
     
     - Specifies the kind of dependency format used for the file at `deps`, if
       specified. Currently supported options are:

       .. list-table::
          :header-rows: 1
          :widths: 20 80
       
          * - Name
            - Description
       
          * - makefile
            - The file should be a Makefile-fragment which specifies a single
              rule. The rule target is ignored by the build system, and the
              dependencies of the rule are treated as dependencies of the
              command which ran.
       
          * - dependency-info
            - The file should be in the "dependency info" format used by some
              Darwin tools (like `ld`).

The build system will automatically create the directories containing each of
the output files prior to running the command.

Shell commands will be rerun any time an input is changed, or an output's state
does not match that of the last time the command was ran. Unlike tools like
*make*, the build system by default will rerun the command on **any** change to
the output file -- even if the output file was just regenerated. This is under
the assumption that the build system can only truly know that a file was
produced correctly if it produces it directly.

.. note::

   One useful behavior not currently supported is the ability to modify and
   rerun individual commands. When using tools like *make* or *ninja*, the build
   system transparently allows this, which can be useful when experimenting with
   individual build flags. However, by design this breaks the consistency of the
   build -- it is no longer strictly determined by the inputs.

   We currently do not support that behavior directly, but may in the future add
   additional options for developers needing to experiment at that level.

       
Clang Tool
----------

**Identifier**: *clang*

A tool used to invoke the Clang compiler. This tool handles the automatic
ingestion of "discovered dependencies" generated by the `-MF` set of compiler
options. When used, the client should provide the path to the generated
dependencies file under the `deps` attribute, and should add the appropriate
compiler options to cause the compiler to generate dependencies at that path.

.. note::

   FIXME: Currently, this tool has no Clang specific behaviors, and works with
   any GCC-compatible compiler. In the future, we anticipate integrating Clang
   more deeply (perhaps through a library API) in order to surface more
   advanced compiler features. At that point, it may make sense to factor out a
   common GCC-compatible tool for use with any such compiler, and keep the
   Clang tool as a more specialized variant.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Name
     - Description

   * - args
     - A string or string list indicating the command line to be executed. If a
       single string is provided, it will be executed using ``/bin/sh -c``.

   * - deps
     - The path to a Makefile fragment (presumed to be output by the compiler)
       specifying additional discovered dependencies for the output.

Swift Compiler Tool
-------------------

**Identifier**: *swift-compiler*

A tool used to invoke the Swift compiler. This tool handles the construction of
the additional arguments necessary to invoke the Swift compiler directly for use
with incremental dependencies (e.g., creating the "output file map"), and it
will automatically track the discovered dependencies from the Swift compiler
(e.g., the header files used via the Clang importer).

Commands using the Swift compiler also include an automatic dependency on the
exact version of the Swift compiler in use (as reported by ``swiftc
--version``).

.. note::

   FIXME: For now, clients are expected to pass a `-j` argument to the compiler
   explicitly if concurrent compilation is desired. In the future we expect the
   build system and compiler to have a two-way communication to share the system
   resources efficiently, so that the build system is capable of understanding
   the level of parallelism that is actively being used by the compiler.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Name
     - Description

   * - executable
     - A string indicating the path to a ``swiftc`` compiler that will be used
       to compile Swift code.

   * - module-name
     - A string indicating the name of the ``.swiftmodule`` to be output.

   * - module-output-path
     - A string indicating the path at which to output the built
       ``.swiftmodule``.

   * - sources
     - A string or string list indicating the paths of Swift source files to be
       compiled.

   * - objects
     - A string or string list indicating the paths of object files to be
       linked when compiling the source files.

   * - import-paths
     - A string or string list indicating the path at which other imported
       Swift modules exist.

   * - temps-path
     - A string indicating the path at which temporary build files are to be
       placed.

   * - is-library
     - A boolean indicating whether the source files should be compiled as a
       library or an executable. Specify ``true`` for a library, ``false``
       for an executable.

   * - other-args
     - A string or string list indicating other arguments passed to the
       ``swiftc`` executable. Examples of individual values include
       ``"-enable-testing"`` or ``"-Onone"``.

   * - enable-whole-module-optimization
     - A boolean indicating whether to enable pass ``-whole-module-optimization``
       flag to swiftc.

   * - num-threads
     - An integer which enables multithreading if greater than 0 and specifies 
       the number of threads to use. Sets swiftc's ``-num-threads`` flags.

Archive Tool
------------

**Identifier**: *archive*

A tool used to create an archive (``.a``)

All non-virtual inputs are archived. Only one non-virtual output may be
specified, this is inferred to be the archive file that this tool produces.

A typical use for this tool is creating static libraries.

.. note::

   FIXME: currently the archive is always recreated entirely, it would be
   preferable in future to correctly update/delete/create the archive file
   as required.

Stale File Removal Tool
-----------------------

**Identifier**: *stale-file-removal*

A tool to remove stale files from previous builds.

The build system records the last value of `expectedOutputs` and compares it
to the current one. Any path that was previously present, but isn't in the
current string list will be removed from the file system.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Name
     - Description

   * - expectedOutputs
     - A string list of paths that are expected to be produced by the given
       manifest.

   * - roots
     - A string lists of paths that are the only allowed root paths for files
       to be deleted. Files outside of those paths will not be removed by
       stale file removal.

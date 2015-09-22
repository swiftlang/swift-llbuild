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

Build Graph
===========

Conceptually, the ``BuildSystem`` is organized around a bipartite graph of
commands and nodes. A ``Command`` represent a units of computation that need to
be done as part of the build, and a ``Node`` represent a concrete value used as
an input or output of those commands. Each ``Command`` declares which ``Tool``
is to be used to create it. The tools themselves are integrated into the
``BuildSystem`` component (either as builtin tools, or via the client), and this
allows them to provided customizable features for use in the build file, and to
be deeply integrated with *llbuild*.

Nodes
-----

Each node represents an individual value that can be produced as part of the
build. In a file based context, each node would conceptually represent a file on
the file system, but there is no built-in requirement that a node be synonymous
with a file (that said, it is a common case and the ``BuildSystem`` will have
special support to ensure that using nodes which are proxies for files on disk
is convenient and featureful).

Currently, the build system automatically treats nodes as files unless they have
a name matching ``'<.*>'``, see the documentation of the ``is-virtual`` node
attribute for more information.

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

  The default target to build can be specified by including an entry for the
  empty string (`""`).
  
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

   * - is-virtual
     - A boolean value, indicating whether or not the node is "virtual". By
       default, the build system assumes that nodes matching the pattern
       ``'<.*>'`` (e.g., ``<link>``) are virtual, and all other nodes correspond
       to files in the file system matching the name. This attribute can be used
       to override that default.

.. note::
  FIXME: At some point, we probably want to support custom node types.


Builtin Tools
=============

The build system provides several built-in tool definitions which are available
regardless of the client.

The following tools are currently built in:

Phony Tool
  **Identifier**: *phony*

  A dummy tool, used for imposing ordering and grouping between input and output
  nodes.

  No attributes are supported other than the common keys.

Shell Tool
  **Identifier**: *shell*

  A tool used to invoke shell commands.
 
  This tool supports an "args" key used to provide the shell command to be run
  (using ``/bin/sh -c``).


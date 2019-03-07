# Build System Tool Extensions

```eval_rst
.. note::

  NOTE: BuildSystem extensions are an **experimental** (i.e. not even **alpha**
  quality) llbuild feature; they are intended for exploring the concepts around
  integrating tools into the build process, but should not be used for
  production purposes. In particular, the API and ABI **will change**.
  
```

## Overview

llbuild's `BuildSystem` component supports the concept of extensions
(i.e. plugins) which can be used to extend llbuild's functionality, typically in
order to perform the build faster.

As with most build systems, most of the build work is performed in today's world
by delegating to external command line tools which are provided by a wide range
of projects. The extension system is designed to allow a transparent adoption of
extension behavior, while also allowing those external projects to be decoupled
from llbuild (i.e. support for llbuild can be something the project adds as a
benefit to llbuild users, without the llbuild project needing to be aware of it,
and without needing a complex system-wide registration process).

### Extension Discovery

llbuild dynamically discovers the presence of an "extension" for a particular
tool, but searching the file system for an additional extension configuration
executable adjacent to the tool in the file system. This executable is
identified by appending `-for-llbuild` to the original tool name. This mechanism
allows projects to transparently provide llbuild extension support simply by
installing an additional helper tool which can communicate information on the
extension to llbuild. Projects that so desire may even install this file as a
symlink to their main tool, and use `argv[0]` or the llbuild extension
configuration options (see below) to discriminate behavior.

### Extension Configuration Options

Once an llbuild tool extension has been discovered, llbuild will query the
configuration executable to discover the actual details of the extension.

At this time, the configuration options the tool should expect are:

* `--llbuild-extension-version` `0`: The extension version llbuild expects to
  find. This will **always** be the first argument, and can be used to detect
  llbuild queries.
  
* `--extension-path`: The path to the actual extension dynamic shared object
  (DSO) should be reported.

### Extension Entry Point

The extension itself is provided by a DSO which llbuild will load (`dlopen`)
once discovered.

The extension **must** provide a single entry point
`initialize_llbuild_buildsystem_extension_v0` which, at this time, is expected
to directly return a `BuildSystemExtension` instance. This implies that the
extension must have been built with access to the internal `BuildSystem`
libraries. In the future, this will move to be based on the llbuild C API.

For details on the API extension points available, see `BuildSystemExtension`
and related classes.

```eval_rst
.. note::

  FIXME: This needs to be moved to a C API.
  
```

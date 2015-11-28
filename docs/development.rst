
=============
 Development
=============

This document contains information on developing llbuild.

The project is set up in the following fashion, generally following the LLVM and
Swift conventions.

* For C++ code:

  * The code is written against the C++14 standard.

  * The style should follow the LLVM conventions, but variable names use
    camelCase.

  * Both exceptions and RTTI are **disallowed**.


* The project is divided into conceptually distinct layers, which are organized
  into distinct "libraries" under `lib/`. The current set of libraries, and
  their dependencies, is:

  **llvm**

    Shared LLVM support facilities, for llbuild use. These are intended to be
    relatively unmodified versions of data structures which are available in
    LLVM, but are just not factored in a way that we can use them. The goal is
    to eventually factor out a common LLVM-support infrastructure that can be
    shared.

  **Basic**

    Support facilities available to all libraries.

  **Core**

    The core build engine implementation. Depends on **Basic**.

  **BuildSystem**

    The "llbuild"-native build system library. Depends on **Basic**, **Core**.

  **Ninja**

    Programmatic APIs for dealing with Ninja build manifests. Depends on
    **Basic**.

  **Commands**

    Implementations of command line tool frontends. Depends on **BuildSystem**,
    **Core**, **Ninja**.

  Code in libraries in the lower layers is **forbidden** from using code in the
  higher layers.

* Public facing products (tools and libraries) are organized under
  `products/`. Currently the products are:

  **llbuild**

    The implementation of the command line `llbuild` tool, which is used for
    command line testing.

  **libllbuild**

    A C API for llbuild.

  **swift-build-tool**

    The command line build tool used by the Swift package manager.

* Examples of using `llbuild` are available under `examples/`.

* There are two kinds of correctness tests include in the project:

  **LLVM-Style Functional Tests**

    These tests are located under `tests/` and then layed out according to
    library and the area of functionality. The tests themselves are written in
    the LLVM "ShTest" style and run using the `Lit` testing tool, for more
    information see LLVM's [Testing
    Guide](http://llvm.org/docs/TestingGuide.html#writing-new-regression-tests).

  **C++ Unit Tests**

    These tests are located under `unittests/` and then layed out according to
    library. The tests are written using the
    [Googletest](https://code.google.com/p/googletest/) framework.

  All of these tests are run by default (by `lit`) during the build.

* There are also additional performance tests:

  **Xcode Performance Tests**

    These tests are located under `perftests/Xcode`. They use the Xcode XCTest
    based testing infrastructure to run performance tests. These tests are
    currently only supported when using Xcode.

* Header includes are placed in the directory structure according to their
  purpose:

  `include/llbuild/<LIBRARY_NAME>/`

    Contains the *internal* (in Swift terminology) APIs available for use by
    any other code in the *llbuild* project (subject to layering constraints).

    **All** references to these includes should follow the form::

      #include "llbuild/<LIBRARY_NAME>/<HEADER_NAME>.h"

  `lib/llbuild/<LIBRARY_NAME>`

    Contains the *internal* (in Swift terminology) APIs only available for use
    by code in the same library.

    **All** references to these includes should follow the form::

      #include "<HEADER_NAME>.h"

  The Xcode project disables the use of headermaps, to aid in following these
  conventions.

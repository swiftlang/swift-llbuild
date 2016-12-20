
=============
 Development
=============

This document contains information on developing llbuild.

Build Instructions
------------------ 

**Building from source on OSX**

* Install latest Xcode and dependencies::

    $ brew install cmake ninja

* Install FileCheck::

    $ brew install llvm

* Build::

    $ mkdir build && cd build
    $ cmake -G Ninja -DCMAKE_BUILD_TYPE:=Debug ..
    $ ninja

* If cmake errors out with : `Failed to locate 'lit' executable (missing: LIT_EXECUTABLE)`::

    $ export PATH=$HOME/Library/Python/2.7/bin/:"$PATH" 
                  
    and then run cmake or:
                  
    $ env PATH=$HOME/Library/Python/2.7/bin/:"$PATH" cmake -G Ninja -DCMAKE_BUILD_TYPE:=Debug ..

Note: this assumes you have installed `lit` as a user, by running `easy_install --user lit`.

**Building from source on Ubuntu**

* Install dependencies::

    $ sudo apt-get install clang cmake ninja-build sqlite3 python-pip libsqlite3-dev libncurses5-dev
      
* Install lit via pip::

    $ sudo pip install lit

* Install FileCheck::

    $ sudo apt-get install llvm-3.7-tools

* Build::

    $ mkdir build && cd build
    $ cmake -G Ninja -DCMAKE_BUILD_TYPE:=Debug -DCMAKE_C_COMPILER:=clang -DCMAKE_CXX_COMPILER:=clang++ ..
    $ ninja

**Building from source on Windows**

* Install the latest Visual Studio with Visual C++.

* Install the latest version of `CMake <https://cmake.org/>`_ and add `cmake.exe` to the system's PATH
  environment variable.

* Install the latest version of `Ninja <https://ninja-build.org/>`_ and add `ninja.exe` to the system's
  PATH environment variable.

* Configure a developer command prompt, using `amd64` for 64 bit processors and 'x86' for 32 bit processors.
  All the followingcommands should be executed from a developer command prompt.

    $ "C:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/vcvarsall.bat" amd64

* Download the latest `sqlite amalgamation source code <https://sqlite.org/download.html>`_ and extract the source code.

* Build sqlite3, using `/MDd` for a Debug build, and `/MD` for a Release build::
   
    $ cl shell.c sqlite3.c /MDd -Fe:sqlite3.exe
    $ lib shell.obj sqlite3.obj /out:sqlite3.lib

* Build LLVM, in order to use `FileCheck` and `llvm-lit`.

* Build::

    $ mkdir build && cd build
    $ cmake -G "Ninja"^
     -DCMAKE_BUILD_TYPE=Debug^
     -DLIT_EXECUTABLE="<llvm-bin-directory>/llvm-lit.py"^
     -DFILECHECK_EXECUTABLE="<llvm-bin-directory>/FileCheck.exe"^
     -DLLBUILD_PATH_TO_SQLITE_SOURCE="<directory-containing-sqlite3.h>"^
     -DLLBUILD_PATH_TO_SQLITE_BUILD="<directory-containing-sqlite3.lib>"^
     "C:/Users/hbellamy/Documents/GitHub/my-swift/llbuild"
    $ ninja

Notes
-----

The project is set up in the following fashion, generally following the LLVM and
Swift conventions.

* For C++ code:

  * The code is written against the C++14 standard.

  * The style should follow the LLVM conventions, but variable names use
    camelCase.

  * Both exceptions and RTTI are **disallowed**.


* Dependencies:

  * llbuild depends on having a C++14 compatible compiler (although
    we do maintain some workarounds to support older versions of GCC/libstdc++
    that were not fully compliant).

  * llbuild depends on having `lit` and `FileCheck` available for executing our
    tests. Currently, the CMake system requires that these dependencies be
    satisfied to complete.


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

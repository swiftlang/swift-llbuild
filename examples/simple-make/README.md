Simple Make
===========

This is a demonstration of how to use ``llbuild`` to construct a simple build
system that compiles all of the source files in a directory into one executable.

All of the examples use a simple framework ``simplebuild`` on top of llbuild
that uses JSON-encoded representations for all of the keys and values, supports
asynchronous execution, and that automatically dispatches to an appropriate
``Rule`` based on information encoded in the key. It also collapses the
distinction between ``Rules`` and ``Tasks`` for simplicity. ``llbuild`` is
currently designed with the expectation that most clients will have some similar
kind of framework.


Installing
----------

To run these examples, you will need:

1. A built copy of ``llbuild.dylib`` in
   ``${LLBUILD}/build/lib/libllbuild.dylib`` (the default location if you use a
   CMake/Ninja build in a ``build`` subdirectory of the project).

2. The ``cffi`` Python module available.

3. The ``llbuild`` Python module available.

The easiest way to accomplish this is with::

~~~ shell
# Configure and build llbuild in the expected location.
cd path/to/llbuild
mkdir -p build
cd build
cmake -G Ninja ..
ninja

# Install virtualenv, if not present.
sudo easy_install virtualenv

# Create a virtualenv and install ``cffi`` into it.
#
# We set CFLAGS to work around CFFI not properly getting the right include
# paths if there is no root SDK.
cd path/to/llbuild/examples/simple-make
virtualenv venv
env CFLAGS="-I$(xcrun --show-sdk-path)/usr/include/ffi" venv/bin/pip install cffi
source venv/bin/activate

# Ensure Python can find the ``llbuild`` module.
export PYTHONPATH=path/to/llbuild/bindings/python
~~~

Examples
--------

The specific examples are:

1. ``count-lines-1 <FILE_PATH>``

   This is a minimal demonstration of the ``simplebuild`` framework that counts
   the number of lines of an input file path.

2. ``count-lines-2 <FILE_PATH>``

   This is an extension of ``count-lines-1`` that adds external-synchronization
   on the file system state of the input, so that we can persist the count in a
   database.

3. ``count-lines-3 <DIR_PATH>``

   This is an extension of ``count-lines-2`` that shows how to count the lines
   for all files in a directory with appropriate dependencies so that the count
   is recomputed when the directory or the files change.

4. ``count-lines-4 <DIR_PATH>``

   This is an extension of ``count-lines-3`` that shows how we would deal with
   counting the lines using an external tool.

5. ``simple-make <DIR_PATH>``

   This is an extension of the above examples to build all of the source files
   in a directory into an executable. It handles automatically rebuilding when
   the source files or the directory change. It does not currently attempt to
   handle other details (like header dependencies of the source files, or making
   sure that the output files are synchronized correctly).
   


  
  

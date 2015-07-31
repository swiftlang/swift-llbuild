=============
 Simple Make
=============

This is a demonstration of how to use ``llbuild`` to construct a simple build
system that compiles all of the source files in a directory into one executable.

All of the examples use a simple framework ``simplebuild`` on top of llbuild
that uses JSON-encoded representations for all of the keys and values, supports
asynchronous execution, and that automatically dispatches to an appropriate
``Rule`` based on information encoded in the key. It also collapses the
distinction between ``Rules`` and ``Tasks`` for simplicity. ``llbuild`` is
currently designed with the expectation that most clients will have some similar
kind of framework.

The specific examples are:

1. ``count-lines-1``

   This is a minimal demonstration of the ``simplebuild`` framework that counts
   the number of lines of an input file path.

2. ``count-lines-2``

   This is an extension of ``count-lines-1`` that adds external-synchronization
   on the file system state of the input, so that we can persist the count in a
   database.

3. ``count-lines-3``

   This is an extension of ``count-lines-2`` that shows how to count the lines
   for all files in a directory with appropriate dependencies so that the count
   is recomputed when the directory or the files change.

4. ``count-lines-4``

   This is an extension of ``count-lines-3`` that shows how we would deal with
   counting the lines using an external tool.

5. ``simple-make``

   This is an extension of the above examples to build all of the source files
   in a directory into an executable. It handles automatically rebuilding when
   the source files or the directory change. It does not currently attempt to
   handle other details (like header dependencies of the source files, or making
   sure that the output files are synchronized correctly).
   


  
  

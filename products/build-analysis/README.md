# build-analysis

`build-analysis`  is a collection of tools to perform analysis operations on build data (e.g. from the build database). It provides basic structures for creating commands and parsing arguments.

## Adding new commands

Commands live in the `Commands` target and need to inherit from the class `Command` providing a type as their generic type which conforms to `OptionsType` to define the command line options. For a basic example take a look at `CriticalPathTool`. The command line arguments parsing infrastructure is rudimentary and needs to be enhanced as needed.
The `Command` subclass needs to override the `run()` method to perform any work and can access the initialized `options` property for arguments. To register the command with the running program, its type needs to be added to the list in `main.swift`. Commands are identified by their `toolName` and are considered in order. Only one command can be executed per lifetime of the program.

## Using the tool

The product of this package is a binary which can be executed followed by the command and its options. An example execution could look like this:

```Bash
$ swift build -c release # build the tool in release mode
$ ./.build/release/build-analysis critical-path --database ./build.db
```

# License

Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors.
Licensed under Apache License v2.0 with Runtime Library Exception.

See http://swift.org/LICENSE.txt for license information.

See http://swift.org/CONTRIBUTORS.txt for Swift project authors.

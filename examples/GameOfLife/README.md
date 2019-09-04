# Game Of Life

Conway's "Game of Life", implemented as a build system.

This is an example of how llbuild can be used to perform arbitrary computations.

## Usage

To run the example, run:

```shell
$ swift run
```

This will display a sequence of Game of Life frames in the console. You can
adjust the starting state by modifying
[`Sources/game-of-life/main.swift`](Sources/game-of-life/main.swift).

The animation is created by asking llbuild to "build" each frame. The frame
itself is built by expressing a dependency on each cell in the frame, and each
cell depends on the adjacent neighbors from the previous frame.

This is not an efficient way to compute the Game of Life, but it *is* an easy
way to visualize moderately large build graphs.

This approach also allows some interesting techniques; for example, you can
easily change it to print the frames in reverse order, something a traditional
stateful Game of Life simulation wouldn't naively support.

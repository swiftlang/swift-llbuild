# Game Of Life

Conway's "Game of Life", implemented as a build system.

This is an example of how llbuild can be used to perform arbitrary computations.

## Usage

To run the example, on macOS use:

```shell
$ swift run -Xlinker -lsqlite3 -Xlinker -lncurses LifeServer
```

and on Linux use:

```shell
swift run -Xlinker -lsqlite3 -Xlinker -ltinfo LifeServer
```

This will run a local webserver on [localhost:8080](http://localhost:8080)
showing a Game of Life animation.

The animation is created by asking llbuild to "build" each frame. The frame
itself is built by expressing a dependency on each cell in the frame, and each
cell depends on the adjacent neighbors from the previous frame.

This is not an efficient way to compute the Game of Life, but it *is* an easy
way to visualize moderately large build graphs.

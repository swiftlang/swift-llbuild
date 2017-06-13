# ptreetime

A utility for profiling process tree times.

This currently only has been tested on macOS. It also will only fully work if
DYLD is able to inject interpositioning libraries into the target process (this
won't work on processes with library validation enabled, for example).

# Vendored source

The bridge vendors two BSD-2-Clause projects so the published source tree is
self-contained:

- [`captppd`](https://github.com/darkvision77/captppd), commit
  `d6df4f867d801e9dbe77f7e58eaec3a77f118416`;
- [`libcapt`](https://github.com/darkvision77/libcapt), commit
  `8f1fd502e6fee893d409f09c5895248c189ead4b`.

`captppd/captbackend/Core/CaptPrinter.cpp` and `CaptPrinter.hpp` contain the
Windows USB transport and status-handling changes used by this project.

Large upstream PBM/PDF test fixtures are intentionally excluded from this
repository because they are not required to build the bridge. They remain
available from the upstream repositories at the commits listed above.

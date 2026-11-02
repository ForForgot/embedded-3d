# A/B benchmark against ol3d 1.x

`bench_compare.c` renders the same 1280 triangle sphere through the 1.x
pipeline and through each tier of this library, and prints the comparison table
quoted in the top level README.

The 1.x sources are **not** part of this repository: they belong to a different
project and carry no licence. To build the benchmark, copy the eight files from
https://github.com/ForForgot/ol3d into `bench/legacy/`:

    bench/legacy/ol3d_core.c    ol3d_core.h
    bench/legacy/ol3d_matrix.c  ol3d_matrix.h
    bench/legacy/ol3d_render.c  ol3d_render.h
    bench/legacy/ol3d_vector.c  ol3d_vector.h

then reconfigure and run:

    cmake -S . -B build
    cmake --build build
    ./build/embd3d_bench

The `embd3d_bench` target is only configured when `bench/legacy/ol3d_matrix.c`
exists, so a normal build never depends on it. `math_compat.h` (this directory)
injects the missing `<math.h>` include that the 1.x sources need, so the
snapshot can stay byte identical.

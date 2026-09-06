# What the sixth pass cost, and where

Every number comes from `--frames 40`, which discards six warm-up frames,
collects the rest and prints mean, median, p95, min and max with the stage
breakdown averaged over the same window. Two rasterisers: the machine's own
integrated GPU, an AMD Radeon 780M through Mesa 25.0.7 on the desktop, and
Mesa llvmpipe under Xvfb, which every earlier table in this project was
measured on.

**The before.** 81.30 ms, measured once at the start of this session from
the unmodified `6a40427` tree, with the same command on the same machine
and the same desktop. The fifth pass's own table records 88.9 ms for the
same build under a load average of thirteen. The after is five consecutive
runs, so the spread below is the machine's and not a single sample's.

## The Radeon 780M, 1600 x 900, viewpoint 1

| | `6a40427` | this pass |
|---|---:|---:|
| mean frame | 81.30 ms (12.3 fps) | 59.1 – 63.0 ms (15.9 – 16.9 fps) |
| median frame | 81.69 ms | 58.7 – 62.7 ms |
| draw calls | 1 784 | 1 407 |
| shadow draws | 3 503 | 2 534 |
| triangles drawn | 5.61 M | 7.50 M |
| shadow pass | 28.59 ms | 18.7 ms |
| opaque pass | 43.26 ms | 33.3 ms |
| prepass | 5.14 ms | 4.8 ms |
| post | 3.78 ms | 3.7 ms |
| scene build | 27.6 s | 19.1 – 20.5 s |
| static batches | 1 721 | 1 721 |
| scene triangles at build | 1.56 M | 1.57 M |
| scene mesh memory | 94 MiB | 94 MiB |

A 27 per cent shorter frame with a third more triangles in it, forty-four
authored parked cars instead of about ten, thirty drivers, scanned trees
the length of the street, and collision against every solid in the footway.
The triangles went up because the content got better; the frame went down
because the *draws* went down, which is what this GPU is bound by.

Where the frame goes now, and what is left in it:

| stage | ms | note |
|---|---:|---|
| cull | 0.5 | |
| shadow | 18.7 | 2 534 draws, one per instance per cascade (CNA-F6) |
| prepass | 4.8 | depth and normals for SSAO |
| sky | 0.1 | |
| opaque | 33.3 | 1 407 draws, 7.5 M triangles |
| post | 3.7 | tone map, bloom, SSAO resolve, FXAA |

## What the settings cost, on the same GPU

| | frame | shadow | prepass | opaque | post | draws | shadow draws |
|---|---:|---:|---:|---:|---:|---:|---:|
| everything on | 61.0 ms (16.4 fps) | 18.7 | 4.8 | 33.3 | 3.7 | 1 407 | 2 534 |
| `--no-shadows` | 39.6 ms (25.2 fps) | 0 | 4.6 | 30.8 | 3.6 | 1 404 | 0 |
| shadows, SSAO, bloom, fog and clouds off | 31.9 ms (31.3 fps) | 0 | 0 | 28.3 | 3.1 | 1 404 | 0 |

The shape of the frame is the same as the fifth pass found and the numbers
are all smaller: the shadow pass is 31 per cent of it rather than 40, and
the opaque pass is 33 ms for 1 407 draws and seven and a half million
triangles, which is still about twenty microseconds a draw and a rounding
error a triangle. This frame is bound by draw submission, and every gain in
this pass came from submitting less.

## Three viewpoints, on the same GPU

| viewpoint | frame | draws | shadow draws | triangles |
|---|---:|---:|---:|---:|
| 1 footway looking south (hero) | 61.0 ms | 1 407 | 2 534 | 7.50 M |
| 6 above the junction (wide) | 48.4 ms | 1 307 | 1 613 | 6.54 M |
| 7 the long view south (long) | 60.9 ms | 1 536 | 1 948 | 8.51 M |

## llvmpipe, 1024 x 576, viewpoint 1

Three runs: 55.15, 55.27, 55.62 ms mean (18.0 – 18.1 fps), 1 403 draws,
2 525 shadow draws, 7.50 M triangles, shadow 16.8 – 17.5 ms, opaque
29.4 – 30.1 ms.

There is no like-for-like llvmpipe *before* for this pass, and it would be
dishonest to compare against the fifth pass's llvmpipe table: that was
measured on a machine running other builds at a load average of eight to
fourteen, and this one was quiet. What the two rasterisers say together is
worth more than either number -- llvmpipe at a quarter of the pixels is
within ten per cent of the Radeon at full size, which is what a
submission-bound frame looks like from both ends.

## What each consolidation bought

Exact, and not subject to machine load: these are counts, not times.

| | before | after |
|---|---:|---:|
| Opel Astra GTC, primitives (near / far) | 33 / 15 | 8 / 4 |
| Fiat Punto GT | 25 / 8 | 19 / 5 |
| Honda Civic EK | 21 / 7 | 7 / 3 |
| Mercedes Sprinter | 14 / 10 | 9 / 5 |
| Mini Cooper S | 18 / 5 | 14 / 4 |
| Renault Logan | 14 / 4 | 14 / 4 |
| small price car | 35 / 20 | 12 / 4 |
| VAZ 2104 | 14 / 4 | 10 / 3 |
| **eight cars, total** | **174 / 73** | **93 / 32** |
| a person, draws | 6 | 3 |
| the crowd, draws in frame | 302 | 150 |
| the context district's parked cars | 108 draws | 32 draws |

The Logan does not merge: all four of its materials paint a tiled base
colour, and an atlas cell cannot tile. The Punto and the Mini merge
partly, for the same reason.

## Where the triangles are now

The registered scene's heaviest families, by triangle:

| family | draws | copies | triangles | note |
|---|---:|---:|---:|---|
| context tree | 9 | 162 | 3.58 M | the scanned species' far copies, past 130 m, culled at 300 |
| island tree, near | 3 | 27 | 1.81 M | unchanged |
| potted plant | 4 | 36 | 1.59 M | unchanged; culled at 60 m |
| small tree, near | 3 | 21 | 1.31 M | unchanged |
| context car | 32 | 306 | 1.14 M | the authored far copies, in place of 108 draws of lofts |
| hydrant | 14 | 168 | 1.04 M | unchanged |
| jacaranda, near | 3 | 9 | 0.81 M | unchanged |

The two families worth attacking next are the ones that were already the
heaviest before this pass and still are: the scanned trees at their near
level of detail, and the potted plant. Neither is a *draw* problem -- three
draws and four -- so neither is what this frame is bound by.

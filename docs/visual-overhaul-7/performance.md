# Where the frame goes, measured on the GPU as well as the CPU

Every earlier table in this project had one clock: a CPU stopwatch round
each stage, which measures how long the driver took to *accept* the work
and nothing about the GPU doing it. This pass gave `SceneRenderer` a
`CNA::Graphics::GpuTimer` per stage -- shadow, prepass, sky, opaque --
polled a frame late so it never stalls, and read the post chain's own
per-pass timers beside them. The `--frames` report prints both clocks,
the per-cascade shadow work, the shadow pass attributed by content family,
and the frame's draws by family. Nothing below is inferred from a CPU
number any more.

All measurements: AMD Radeon 780M, Mesa 25.0.7, 1600 x 900, `--frames 40`
(six warm-up frames discarded, 34 averaged), `--no-vsync`, the settings as
shipped (High). **The machine was not quiet.** Another agent ran the CNA
test suites throughout this session -- load average 6 to 20 -- so the
wall-clock numbers here are the best of three consecutive runs and carry
several milliseconds of that noise; the GPU-timer numbers are read from
the hardware and are steady to a few tenths. Where a before/after is
claimed, both sides were measured back to back on the same build with
only the one change between them.

## The frame at session start, and at its end

The flagship viewpoint (1, the footway looking south to the junction).

| | start of session (`9cec90d`) | end of session |
|---|---:|---:|
| mean frame, best of 3 | 50.05 ms (20.0 fps) | 48.75 ms (20.5 fps) |
| median frame | 50.25 ms | 48.03 ms |
| draw calls | 1 405 | 1 436 |
| shadow draw calls | 2 530 | 1 892 |
| triangles drawn | 7.50 M | 7.55 M |
| shadow pass triangles, all cascades | 13.2 M | 6.5 M |
| GPU shadow pass | 14.6 - 15.8 ms | 9.3 - 9.8 ms |
| GPU opaque pass | 14.6 - 19.0 ms | 17.0 - 20.0 ms |
| GPU post chain | 5.3 - 7.3 ms | 5.8 - 7.4 ms |
| static batches | 1 721 | 1 817 |
| scene triangles at build | 1.56 M | 1.57 M |
| mesh memory | 94 MiB | 95 MiB |

The frame did not get much shorter and that is the honest reading of it:
the pass **added** a city behind the frontage (+45 draws, +5.5 k
triangles), thirty skinned drivers in place of thirty rigid props (+1 draw
each inside 30 m), an audio mixer and a second row of cull work, and
**took away** half the shadow pass. The two roughly cancel on the wall
clock under this load; on the GPU timers the shadow pass is 5 to 6 ms
lighter and everything else is where it was. The session-start figure
above was itself measured today, not copied from the sixth pass's table,
which recorded 59 - 63 ms for the same build on a quieter day and a
different load.

## The two clocks, flagship view, end of session

| stage | CPU (submission) | GPU (execution) | what the gap says |
|---|---:|---:|---|
| cull | 0.6 ms | -- | CPU only |
| shadow | 16.0 ms | 9.8 ms | 1 892 draws at ~8 us each on the CPU; the GPU finishes first |
| prepass | 5.8 ms | 1.2 ms | depth and normals for SSAO: five times as long to submit as to draw |
| sky | 0.1 ms | 1.7 ms | one full-screen shader; GPU only |
| opaque | 32.1 ms | 20.0 ms | 1 436 draws; ~22 us each on the CPU |
| post | 3.7 ms | 7.4 ms | six full-screen passes; GPU only |
| **sum** | **58.4 ms** | **40.1 ms** | |

So: **the frame is CPU-bound in the opaque and shadow passes and
GPU-bound in post**, and the two halves overlap in time, which is why the
sum of GPU stages is 69 per cent of the wall clock rather than all of it.
The opaque pass is the biggest single item on both clocks, and on the CPU
it is twelve milliseconds longer than on the GPU: that twelve is the
driver taking 1 436 `PbrEffect::Apply` calls. The previous pass's
diagnosis -- draw submission -- was right; this is the first time it has
been measured rather than argued.

The post chain, by pass, on the GPU:

| pass | ms |
|---|---:|
| SSAO | 2.4 - 2.9 |
| light shafts | 2.1 - 2.7 |
| bloom | 0.8 |
| height fog | 0.4 |
| tone map | 0.4 |
| FXAA | 0.25 |

Light shafts cost as much as SSAO and are the least visible of the six.

## The shadow pass, attributed

This is the table that decided what to do. `shadowReport()` attributes the
shadow pass's own draws and triangles to the name each caster was
registered under, across every cascade it was written into. Flagship view,
one frame, before anything changed:

| family | draws | triangles |
|---|---:|---:|
| tree-hero-island | 39 | 2 613 377 |
| tree-hero | 39 | 2 579 376 |
| tree-hero-jacaranda | 23 | 2 153 456 |
| hydrant | 98 | 604 212 |
| hero-car-honda-civic-ek-r1 | 18 | 445 915 |
| hero-car-opel-astra-gtc-r2 | 11 | 360 708 |
| frame-white (window frames) | 41 | 266 910 |
| ashlar | 33 | 155 448 |
| roof-zinc | 45 | 154 932 |

The three scanned tree species were 7.3 M of a 13.2 M-triangle shadow
pass -- **more than half of it** -- for a canopy silhouette that the far
cascade's ground sample, metres wide, can never resolve to the leaf level
the near mesh draws it at. After the two fixes below:

| family | draws | triangles |
|---|---:|---:|
| tree-hero-island | 39 | 724 386 |
| tree-hero-jacaranda | 23 | 716 232 |
| tree-hero | 39 | 621 638 |
| hydrant | 98 | 604 212 |
| frame-white | 41 | 266 910 |

The trees are 2.06 M, the parked fleet is gone from the top of the table,
and the pass is 6.5 M triangles. The hydrant is now the fourth-heaviest
caster in the street, at 6 200 triangles a copy for a fire hydrant; that
is the next thing on this list, not this pass's.

Per cascade, flagship, end of session:

| cascade | reaches | fit radius | draws | triangles |
|---|---:|---:|---:|---:|
| 0 | 7.3 m | 9.8 m | 152 | 0.58 M |
| 1 | 17.4 m | 22.3 m | 276 | 0.96 M |
| 2 | 45.9 m | 59.1 m | ~550 | 1.86 M |
| 3 | 190 m | 248 m | ~1 030 | 3.09 M |

The far cascade still takes half the pass. Its fit sphere is 248 m across
and contains most of the near geometry too, so a caster near the camera
is written into it as well as into its own cascade; that is correct for a
cascade fit to a frustum slice and it is where the remaining waste is. A
per-cascade caster reach that tests the caster's shadow *extent* against
the slice, rather than its distance from the slice's centre, is the next
step there.

## What was changed, and what each change cost and bought

### 1. The imported models' bounding boxes were cubes

Not a shadow change, but it comes first because it moved everything.
CNA's `ModelMesh` publishes a bounding sphere and no box, and
`ModelLibrary` took a cube of side 2r round it. Every authored car
reported its own length as its width and its height (4.47 x 4.47 x 4.47
for the Astra, against 4.47 x 2.02 x 1.51 measured); every imported prop
culled and cast as though it were its own diagonal. Measured from the
vertices instead (`VertexBuffer::GetDataRawEXT`, a CPU-shadow read at
load). CNA-F18.

Cost: none per frame. Bought: correct collision solids, correct
`fitTo` scaling, tighter culling for every imported model. Not separately
timed -- it landed in the same commit as the traffic and driver fixes --
but the flagship frame went from the 59 - 63 ms the sixth pass recorded
to the 50 measured at the start of this one on the same machine, and
this is the only rendering-side change in between.

### 2. Vegetation and prop shadow LOD

`drawCasters` now prefers `InstanceGroup::lodMesh` unconditionally for
instanced casters, not only past the opaque pass's own switch distance.
The far-ring trees already cast from exactly this mesh; this is that
choice applied to the near ring's shadow only.

| | before | after |
|---|---:|---:|
| shadow pass triangles | 13.22 M | 8.43 M |
| GPU shadow | 15.5 - 15.8 ms | 9.8 ms |
| shadow draws | 2 265 | 2 261 |

Visual difference: none found. A tree's canopy shadow on the pavement
and a parked car's contact shadow were compared at the flagship, the
long-view and the car-close viewpoints.

### 3. A shadow-only proxy for the parked fleet

An authored car's far copy is a differently-merged model (seven parts to
three on the Civic), so the index-matched LOD swap above cannot use it.
`InstanceGroup::shadowOnly` and `CityScene::placeShadowProxy` register the
far copy as a caster with no opaque draw of its own; the shadow effect
reads only positions, so its materials never come into it.

| | before | after |
|---|---:|---:|
| shadow pass triangles | 8.43 M | 6.50 M |
| shadow draws | 2 261 | 1 967 |
| GPU shadow | 9.8 ms | 9.3 ms |

### 4. Architecture and district shadow distance

Buildings and the road cast at the cascades' full reach with no distance
policy of their own; the procedural district beyond the frontage had no
cap at all (0 meant unlimited). `architectureShadowDistance` (150 m) and
`contextShadowDistance` (90 m). Shadow draws 2 479 to 2 265; triangles
barely moved, because what it cut was the far end of the street's
window frames, not anything heavy. Kept because it is right, not because
it is large.

### 5. Pedestrian shadow distance

A person's rigid stand-in cast to 74 m like a bollard: 272 shadow draws
a frame on the flagship view, an eighth of the pass, for figures whose
shadow past forty metres is a pixel of grey under a pixel of person.
`pedestrianShadowDistance`, 38 m. Shadow draws 2 049 to 1 892. Everybody
on the near footway and on the crossing in front of the camera still
grounds (`02-on-the-crossing`).

### 6. One instance buffer per mesh, kept

`InstancedRendererEXT` was constructed on the stack for every group every
frame; a fresh one allocates a `DynamicVertexBuffer` on its first
`setInstances` and frees it when the loop body ends -- fifty-odd GPU
buffer allocations a frame. Cached per mesh. Not measurable through this
machine's load noise on the CPU opaque number, which is why it is listed
last: it is correct and it is free, and its size is below the noise.

### 7. The rows behind the frontage

Two rows of blocks behind the street-facing district, both streets, both
arms: +5.5 k triangles, and +45 draws after being batched a strip at a
time (the first cut, one cull cell per block, was +156). They cast only
inside 60 m.

## What was tried and not kept

* **A flat margin on the far cascade's caster reach.** Same finding as
  the sixth pass: it lets the dense near street into the far cascade.
* **Material state caching in `applyMaterial`.** Not attempted after
  reading the effect: `PbrEffect::Apply` uploads its whole parameter
  block whether or not a property changed, so skipping the setters on
  this side saves the setters and not the upload. The upload is the
  cost. This is the CNA boundary the opaque pass now sits against: 1 436
  draws at ~22 us of submission each, and nothing on this side of the
  effect makes an `Apply` cheaper. Fewer draws is the only lever left,
  and the families with the most draws are the skinned people (153, one
  palette each, CNA-F6's shape) and the static architecture (roof, frame,
  glazing and interior batches at 20 - 30 draws each).
* **Merging the district infill into the frontage collector.** It would
  have cast at 90 m like the frontage; behind a row of buildings that is
  waste, so it has a collector and a cap of its own.

## Three viewpoints, end of session

| viewpoint | frame | draws | shadow draws | triangles | GPU shadow | GPU opaque | GPU post |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 footway looking south (hero) | 48.8 ms | 1 436 | 1 892 | 7.55 M | 9.8 | 20.0 | 7.4 |
| 6 above the junction (wide) | 36.9 ms | 1 369 | 1 110 | 6.16 M | 5.5 | 12.5 | 6.1 |
| 7 the long view south (long) | 49.3 ms | 1 596 | 1 465 | 8.38 M | 9.7 | 19.2 | 6.4 |

## What the settings cost, flagship view

| | frame | GPU shadow | GPU opaque | GPU post | draws | shadow draws |
|---|---:|---:|---:|---:|---:|---:|
| everything on | 48.8 ms | 9.8 | 20.0 | 7.4 | 1 436 | 1 892 |
| `--no-shadows` | 32.6 ms | 0 | 12.9 | 5.8 | 1 435 | 0 |

Shadows are a third of the frame. Note the opaque pass is 7 ms cheaper
on the GPU without them too -- that is the receiver-side sampling, four
cascades with PCF, in every opaque fragment.

## Content in the frame

Flagship view, one frame: vehicles 65 draws / 378 k triangles; drivers 3
draws (one car inside 30 m); characters 153 draws / 558 k triangles;
character shadow proxies 77 draws (was 272).

### The eight authored cars

From the import log and the wheel split, per copy:

| vehicle | near parts | far parts | wheels: rolling / bolted | near triangles | far triangles | draws moving, inside 32 m |
|---|---:|---:|---:|---:|---:|---:|
| Opel Astra GTC | 8 | 4 | 4 / 0 | 150 k | 18 k | 8 + 4 |
| Fiat Punto GT | 19 | 5 | 7 / 7 | 35 k | 14 k | 19 + 14 |
| Renault Logan | 14 | 4 | 4 / 6 | 33 k | 14 k | 14 + 10 |
| VAZ 2104 | 10 | 3 | 4 / 4 | 27 k | 14 k | 10 + 8 |
| Honda Civic EK | 7 | 3 | 4 / 0 | 120 k | 18 k | 7 + 4 |
| small price car | 12 | 4 | 4 / 4 | 42 k | 14 k | 12 + 8 |
| Mini Cooper S | 14 | 4 | 8 / 4 | 35 k | 14 k | 14 + 12 |
| Mercedes Sprinter | 9 | 5 | 4 / 0 | 38 k | 14 k | 9 + 4 |

"Bolted" parts are the arch liners, calipers and mudflaps the wheel
splitter swept up with the wheels; they now turn with the stub axle and
do not roll, which is the wobble fix. They cost a draw each while the car
is inside 32 m; folding them into the body node in
`scripts/blender-vehicles.py` would give them back and is the next thing
to do to these files. Past 32 m every moving car is its far copy (3 - 5
draws) and casts from it; every parked car casts from it at any distance.

### People

Near (inside 20 m): 3 draws, full parts, shadow proxy. Mid (to 38 m): 3
draws, collapsed parts, shadow proxy. Far (to 130 m): 3 draws, collapsed
parts, no shadow. Skinned draws cannot be instanced (each carries its
own palette); 51 people in frame is 153 draws at any distance and this is
the largest draw family in the street by a factor of four. A driver is
the same figure at the collapsed 3 draws, inside 30 m, moving cars only.

## Where the CPU goes that the renderer does not time

`--frames` with and without `--no-audio` differ by less than the run-to-run
noise; the mixer runs on its own thread and `SoundScape::update` is a
few dozen distance tests a frame.

## llvmpipe

Not re-measured this pass. The sixth pass's finding stands: llvmpipe at a
quarter of the pixels lands within ten per cent of the Radeon at full
size, which is what a submission-bound frame looks like from both ends,
and the GPU timers above are the direct evidence for the same thing.

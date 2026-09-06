# Where the frame goes, the eighth pass: the shadow pass halved, the district batched, and the draw cost measured to the microsecond

Every number below is from `--frames 40` or `--benchmark` on the machine's
own AMD Radeon 780M (Mesa 25.0.7) at 1600 x 900, the settings as shipped
(High, four cascades at 2048 px from `render.json`), `--no-vsync`,
`--no-audio`. **The machine was busy throughout**: other sessions kept the
one-minute load average between 5 and 17, and on this APU that moves two
things at once -- the CPU submission time, which scales almost linearly
with the contention, and the GPU clock, which shares the package's power
budget with the CPU. So the wall clock swings by a factor of two between
runs of the same build (69 ms and 111 ms for the same view, ten minutes
apart), the GPU timers by ten to twenty per cent, and the *counts* -- draws,
shadow draws, triangles, per-cascade work -- not at all. Where a
before/after is claimed the counts carry it, the GPU timers support it, and
the wall clock is reported as the range it was.

Each table names the load average the run started under, because on this
machine that is the first column to read.

## The frame at session start

Three viewpoints, three runs each, HEAD `9ac6ba2` before anything changed.

| view | load | wall clock, mean | GPU shadow | prepass | sky | opaque | post | GPU sum | draws | shadow draws | triangles |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 footway south (hero) | 11.8 / 9.8 / 13.5 | 104.8 / 110.0 / 124.3 ms | 10.4 / 10.6 / 11.5 | 1.3 | 2.0 | 24.2 / 24.1 / 25.4 | 8.7 / 9.0 / 10.2 | 46.5 / 46.8 / 50.6 | 1 443 | 1 899 | 7.57 M |
| 6 above the junction | 14.6 / 12.2 / 8.9 | 107.9 / 82.4 / 80.2 | 6.8 / 7.5 / 6.1 | 1.4 - 2.2 | 0.6 - 1.2 | 20.7 / 19.6 / 19.7 | 9.7 / 8.3 / 9.4 | 40.0 / 37.9 / 37.3 | 1 385 - 1 401 | 1 103 - 1 471 | 6.2 - 6.8 M |
| 7 the long view south | 8.6 / 11.4 / 7.4 | 85.8 / 87.2 / 97.1 | 10.1 / 10.2 / 11.3 | 1.3 - 1.6 | 2.0 | 23.6 / 23.5 / 26.3 | 7.6 / 7.3 / 8.8 | 44.6 / 44.3 / 50.0 | 1 590 | 1 449 | 8.42 M |

Viewpoint 6's draw counts differ between runs because `--frames` runs on
the wall clock and the traffic is wherever the clock left it, which is one
of the reasons the benchmark mode below runs at a fixed step.

The post chain, GPU, flagship view: SSAO 3.5, light shafts 3.0, bloom 0.9,
height fog 0.4, tone map 0.4, FXAA 0.4 ms -- 8.7 ms, of which SSAO and
light shafts are three quarters.

Per cascade, flagship view, session start:

| cascade | reaches | fit radius | draws | triangles |
|---|---:|---:|---:|---:|
| 0 | 7.3 m | 9.8 m | 151 | 0.58 M |
| 1 | 17.4 m | 22.3 m | 277 | 0.96 M |
| 2 | 45.9 m | 59.1 m | 555 | 1.77 M |
| 3 | 190 m | 248 m | 916 | 2.58 M |

## 1. A caster is written only into the cascades its shadow can reach

The receiver picks a cascade by view depth. The seventh pass's cull asked
whether a caster was near a cascade's slice; this pass asks whether
anything the caster can shade lies at the depths the cascade is read for,
by sweeping the caster's sphere along the light until its top passes below
the ground and testing that volume's depth range against the slice, padded
by the blend band (`SceneRenderer::casterShadowReachesSlice`,
`tests/ShadowCullTests.cpp`). A caster narrower than one texel of the
cascade is left out as well. The hydrant -- 6 200 triangles a copy, fourth
in the whole pass by triangle -- now casts from the generated hydrant at a
hundred and fifty.

Same three viewpoints, two runs each, same build otherwise:

| view | load | wall clock | GPU shadow | shadow draws | shadow triangles | casters left out per frame |
|---|---:|---:|---:|---:|---:|---:|
| 1 hero | 5.5 / 9.5 | 69.5 / 111.0 ms | **6.6 / 7.2** (was 10.4 - 11.5) | **874** (was 1 899) | **2.22 M** (was 5.90 M) | 922 by slice, 9 by texel |
| 6 above | 10.9 / 16.7 | 139.2 / 97.1 | **6.4 / 4.9** (was 6.1 - 7.5) | **768** (was 1 103 - 1 471) | **2.13 M** (was 3.38 - 4.30 M) | 330 / 4 |
| 7 long | 12.1 / 10.0 | 96.2 / 93.7 | **8.3 / 8.0** (was 10.1 - 11.3) | **775** (was 1 449) | **2.81 M** (was 6.24 M) | 578 / 2 |

Per cascade, flagship view, after:

| cascade | reaches | draws | triangles | was |
|---|---:|---:|---:|---|
| 0 | 7.3 m | 107 | 0.38 M | 151 / 0.58 M |
| 1 | 17.4 m | 141 | 0.39 M | 277 / 0.96 M |
| 2 | 45.9 m | 262 | 0.63 M | 555 / 1.77 M |
| 3 | 190 m | **365** | **0.82 M** | 916 / 2.58 M |

The far cascade is a third of what it was, and it is now the first cascade
whose contents are what it is *for*: the buildings, trees and parked cars
between 46 and 190 m, and no longer the whole near street re-rasterised
into it. The character shadow proxies went from 110 draws a frame to 12 --
each person is now written into the one cascade their shadow lands in --
and the texel floor removed under ten casters a frame, which is what it
should remove: it is a correctness floor, not a saving.

No pixel of shadow moved. The eighteen named viewpoints were captured at
1024 x 576 by this build with only the shadow change in it and by a copy of
the HEAD binary, at the same frame counts of the fixed-step clock, and
compared pixel for pixel: **fourteen are identical**, and the four that are
not differ by 635, 228, 59, 1 and 1 pixels -- every one of them on the
footway under a hydrant, where the generated proxy's silhouette is a little
fuller than the scan's (a mean of 36/255 darker over those pixels), which
is the proxy doing its job. (The committed `docs/screenshots` set reads
0.1 - 0.9 per cent different from either binary, on the cars' wheels: it
predates `9ac6ba2`'s wheel-axle fix by one commit. It is regenerated at the
end of this pass.)

What still dominates the far cascade, flagship view, by triangle: the three
tree species from their far level of detail (0.68 M over 34 draws), the
frontage's window frames (0.19 M), the ashlar and zinc of the frontage
(0.29 M), and the parked cars' far copies (0.1 M each). All of it is
content between 46 and 190 m whose shadow is on screen. The next reduction
there is a coarser shadow level of detail for the frontage architecture --
a caster mesh without reveals and frames -- which is content work, or an
instanced caster program, which is CNA-F6.

## 2. Where the opaque pass's submission time goes

`--frames` now times the opaque pass in two halves: this side's material
setters and `PbrEffect::Apply`, and the framework's draw call. Every view,
every run:

| view | load | setters + Apply | per call | framework draws | per draw | skinned draws | applies | repeating the previous material |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 hero | 5.5 | 1.00 ms | 0.8 us | 34.8 ms | 27.1 us | 9.5 ms | 1 285 | 134 |
| 6 above | 10.9 | 2.02 ms | 1.6 us | 65.0 ms | 52.9 us | 23.7 ms | 1 229 | 118 |
| 7 long | 12.1 | 1.40 ms | 1.0 us | 47.6 ms | 33.8 us | 14.8 ms | 1 407 | 150 |

This is the number the seventh pass could only argue for. Of the opaque
pass's submission time, **three per cent is on this side of the framework
and ninety-seven is inside the draw**; and of the 1 300 - 1 400 effect
applies a frame, about a tenth repeat the previous material and
environment, so a material state cache here could collapse a hundred and
thirty calls and save a tenth of a millisecond. The per-draw cost is the
renderer's -- `BindDrawParams` re-uploading every uniform and rebinding
every texture unit per draw, with the vertex declaration re-described round
it -- and it rises with the machine's load, which is what a driver call
competing for a core does. `docs/cna-findings.md` CNA-F19 has the reading
of the framework source and the proposed fix. Fewer draws is the only lever
on this side, and §3 pulls it.

## 3. Fewer draws: the district as strips, the skyline as sectors, the parked cars per instance

Three consolidations, all of static geometry that shares its render state
and never moves, none of them a single city mesh:

* **The district a strip at a time.** The windowed rows were one 34 m cell
  per block per material -- forty blocks of a dozen materials -- and are
  now one cell per arm, per side, per ninety-five metres. A strip is in
  view or out of it from almost anywhere a camera stands, and its plots
  are a few hundred triangles each, so the cull granularity given up costs
  nothing measurable.
* **The skyline by sector.** Ninety painted blocks at 240 - 430 m each had
  a cell of their own: three or four draws per block for thirty triangles
  and a haze. Batched by thirty-degree sector, two facades a sector.
* **The parked cars per instance.** The four rings the seventh pass dealt
  each model into -- a model's parts times the rings in view, and no ring
  ever reaching the far copy -- are two groups either side of 45 m
  (`InstanceGroup::minDistance`): the near model inside, the welded far copy
  beyond, the far group carrying every copy's shadow. The merge of the
  near copy's parts by material, which would take the Punto from nineteen
  draws to five, is written and dormant on CNA-F20 (below).

The ground plane batches at 230 m rather than 152 (21 draws of flat plane
to about 8). Final build, two runs each, against the session start:

| view | draws | shadow draws | triangles | effect applies | GPU shadow | GPU opaque | GPU post | GPU sum | wall clock (load) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 hero | **1 212** (was 1 443) | **932** (was 1 899) | **6.79 M** (was 7.57 M) | 1 057 (was 1 285) | 6.4 - 6.7 (was 10.4 - 11.5) | 14.1 - 16.4 (was 24.1 - 25.4) | 5.5 - 6.6 (was 8.7 - 10.2) | **28.3 - 32.2** (was 46.5 - 50.6) | 50.3 / 60.7 ms (load 20 / 38) |
| 6 above | **1 158** (was 1 385 - 1 401) | **802** (was 1 103 - 1 471) | **5.46 M** (was 6.2 - 6.8 M) | 1 002 | 5.1 (was 6.1 - 7.5) | 12.6 - 14.7 (was 19.6 - 20.7) | 6.5 - 6.9 (was 8.3 - 9.7) | **26.1 - 28.8** (was 37.3 - 40.0) | 47.6 / 64.6 (load 9 / 12) |
| 7 long | **1 407** (was 1 590) | **807** (was 1 449) | **7.78 M** (was 8.42 M) | 1 222 | 7.5 - 7.8 (was 10.1 - 11.3) | 16.7 - 17.9 (was 23.5 - 26.3) | 5.9 - 6.0 (was 7.3 - 8.8) | **32.5 - 34.5** (was 44.3 - 50.0) | 58.5 / 63.6 (load 10 / 14) |

Two readings of that table, one confident and one not. The counts are
confident: 230 fewer draws on the hero view and 180 - 240 on the others,
about half the shadow draws everywhere, a tenth fewer triangles on the
street views (the parked cars beyond 45 m now draw their far copies). The
GPU column is not a clean before/after: the opaque pass did get cheaper --
fewer draws, fewer triangles, and the shadow *receiver* samples a smaller
atlas footprint -- but the post chain renders the same pixels on both sides
and also reads 30 per cent faster, which is the APU's GPU clock following
the CPU load rather than anything this pass did. Read the GPU sum as "the
frame's GPU work is now about 30 ms rather than 45 on this machine", not as
a measured 35 per cent.

What the scene registers: 1 507 static batches where there were 1 817, and
106 MiB of mesh memory where there were 95 -- the district's plots are
about a hundred thousand triangles of real openings where the blocks were
twenty thousand, and the parked cars' far copies are now instance groups of
their own.

The submission side, flagship view: the framework's draws went from 34.8 ms
(1 285 applies at 27 us) to 23.4 - 28.5 ms (1 057 applies at 22 - 27 us) --
the same per-draw cost for fewer draws, which is exactly what CNA-F19
predicts. This side's setters stayed at 0.7 - 0.9 ms.

## Vehicles: the eight authored cars, audited again

From the import log, per model. "Rolling" parts turn with the road,
"bolted" ones -- arch liners, calipers, mudflaps the wheel splitter swept up
-- turn with the stub axle and stand still; both cost a draw each while a
moving car is inside 32 m, and the classification tests of the seventh pass
still hold (`vehicle_orientation`, `gait_tests`, the walkthrough's heading
check).

| vehicle | near parts | far parts | wheels rolling / bolted | axle off X | draws moving inside 32 m | draws parked inside 45 m | parked beyond 45 m |
|---|---:|---:|---:|---:|---:|---:|---:|
| Opel Astra GTC | 8 | 4 | 4 / 0 | 20.0 deg | 8 + 4 | 8 | 4 |
| Fiat Punto GT | 19 | 5 | 7 / 7 | 25.1 | 19 + 14 | 19 | 5 |
| Renault Logan | 14 | 4 | 4 / 6 | 4.6 | 14 + 10 | 14 | 4 |
| VAZ 2104 | 10 | 3 | 4 / 4 | 0.0 | 10 + 8 | 10 | 3 |
| Honda Civic EK | 7 | 3 | 4 / 0 | 4.7 | 7 + 4 | 7 | 3 |
| small price car | 12 | 4 | 4 / 4 | 4.9 | 12 + 8 | 12 | 4 |
| Mini Cooper S | 14 | 4 | 8 / 4 | 7.0 | 14 + 12 | 14 | 4 |
| Mercedes Sprinter | 9 | 5 | 4 / 0 | 0.0 | 9 + 4 | 9 | 5 |

The last two columns are this pass's change. A parked copy used to draw its
near parts at every distance inside 158 m -- the index-matched swap
`placeProp` offers could not use the far copy, which is a differently
merged model, so no parked car ever reached it -- and was dealt into four
rings of the street so one near car could not promote every copy of its
model. Now a near group culled beyond 45 m and a far group culled inside
it (`InstanceGroup::minDistance`) give each copy the detail its own
distance deserves, the rings are gone, and the far group carries every
copy's shadow. The merge that would take the near copy from 8 - 19 draws
to 3 - 6 is written and dormant: the loader gives every part its own
texture objects (CNA-F20), so no two parts compare equal.

What stays as it was: rolling, steering, direction, driver side, drivers,
glass and lamps -- the moving copies are untouched, and the walkthrough in
§6 says so.

## People

153 - 158 skinned draws a frame on the flagship view, 183 on the long view,
three per figure at any distance, as the seventh pass left them; 556 -
745 k triangles. What changed is their shadow: the rigid proxies were
written into every cascade whose sphere they were near -- 110 draws a
frame -- and are now written into the one cascade their shadow lands in:
10 - 14 draws. Going below three draws a figure needs a bone palette the
effect can instance from (CNA-F14's neighbour) and is not attempted here.

## 4. The post chain, pass by pass

The pipeline's own per-pass timers, read through `Stats::gpuPostPasses`
and written by the benchmark as `gpuPostPasses`. Six presets, final build,
one run each:

| preset | post total | SSAO | light shafts | bloom | height fog | tone map | FXAA |
|---|---:|---:|---:|---:|---:|---:|---:|
| baseline | 6.78 ms | 2.33 | 2.46 | 0.87 | 0.43 | 0.41 | 0.28 |
| shadow | 5.95 | 2.16 | 1.99 | 0.79 | 0.39 | 0.38 | 0.24 |
| traffic | 5.57 | 1.86 | 1.90 | 0.80 | 0.39 | 0.37 | 0.23 |
| crowd | 7.93 | 2.94 | 2.79 | 1.01 | 0.45 | 0.44 | 0.31 |
| city | 7.83 | 3.32 | 2.46 | 0.89 | 0.42 | 0.41 | 0.33 |
| post | 5.42 | 2.14 | 1.73 | 0.72 | 0.32 | 0.31 | 0.20 |

Every pass is a full-screen pass and every row renders the same number of
pixels, so the spread down a column -- tone map 0.31 to 0.44 for identical
work -- is the GPU clock following the machine's load, and the *ratios*
within a row are the honest breakdown: SSAO and light shafts are each
about a third of the chain, bloom an eighth, fog, tone map and FXAA the
rest. `report.md` §8 has the variant runs on the `post` preset -- SSAO at
8 and 12 samples, bloom at 2 and 3 levels, shafts off, and a control run of
the shipped settings, and the same seven on `baseline` -- and what they
showed: the shafts are the large dial (2.1 - 2.5 ms), eight SSAO samples
save about 0.7 ms on the baseline view and are inside the noise on the
`post` preset, and the bloom depth costs nothing. Nothing changed by
default.

## 5. The benchmark presets

`report.md` §9 has the six presets on the final build and an example
result line; `docs/visual-overhaul-8/benchmark/radeon-780m.csv` and
`.json` are the committed baseline, one row per preset, with the load
average beside every number. `README.md` under *Benchmarking* says how to
rerun them.

## 6. Validation

`report.md` §10: CTest 16 of 16, the walkthrough's six legs to the
centimetre of the seventh pass's, the lineup's drivers, and the eighteen
viewpoints against a capture from the HEAD binary.

# The eighth pass: the district finished, the waste removed, the rest written down

The brief for this pass was narrower than the seven before it, and meant to
be: finish the distant city where it was still visibly weaker, remove the
application-side waste that could be removed cleanly, make the street a
benchmark somebody can rerun, and write down -- with measurements -- what
now belongs in CNA rather than here. No new hero assets, no new
post-processing, no new shader features. Every substantial change below
answers one of the four questions the brief asked: does it remove a
remaining visual weakness, does it remove measurable waste, does it make the
benchmark more deterministic, or does it produce evidence for CNA.

Baselines, recorded before anything changed:

| Repository | Baseline | Note |
| --- | --- | --- |
| `cna-street` | `9ac6ba2` on `develop` | The commit this pass starts from |
| `cna` (`../cnanext`) | `next` `6e22a44b6` at the start, `4bf74e31b` by the end | Read-only; another agent committed to it during this session |
| `sharp-runtime` (`../sharp-runtimenext`) | `next` `30ccdef3` | Read-only |
| `easy-gl` | `develop` `deda7a4` | Read-only; an untracked `VERSION` predates this session |
| `meta-gl` | `develop` `20c8b2d` | Read-only; a staged `VERSION` predates this session |

**No framework repository was modified by this session.** Every change in
this pass is in `cna-street`. The only commands run in any sibling were
read-only: `git status`, `git log`, `git rev-parse`, and reads of source
files (`PbrEffect.cpp`, `Effect.cpp`, `EasyGLRenderer.cpp`,
`CascadedShadowMap.cpp` and headers) to find out *why* a measurement came
out as it did. At the start of the session `../cna` and `../cnanext` both
showed working-tree edits to their planning documents (`NEXT.md`,
`integration/*.md` and the like); those are another agent's, present
before this session began, and are recorded here so nobody attributes them
to this one. One new CNA limitation was found and is recorded as CNA-F19 in
`docs/cna-findings.md`; three behaviours worth knowing are recorded under
the eighth pass's notes there.

---

## 1. What was still worth doing

The seventh pass ended with three named weaknesses: the district's facades
up close, the far cascade taking half the shadow pass, and the skinned
crowd's draw count. The first two are application work and this pass did
them. The third is CNA's, and this pass measured its boundary rather than
pushing at it.

Underneath those, one open question: twelve milliseconds of the opaque pass
had been attributed to `PbrEffect::Apply` from the difference between two
clocks. Whether that time was on this side of the framework -- in the
twenty-five property setters a material costs -- or inside the draw decides
whether a material cache here would buy anything. It had never been timed.

## 2. What was deliberately not touched

Anything whose fix is in CNA. The per-draw upload in the renderer's
`BindDrawParams` (CNA-F19, below); the shadow caster program's lack of an
instanced variant (CNA-F6); the skinned effect's per-figure bone palette
(CNA-F14's shape), which is why a person is three draws at any distance
and fifty people are a hundred and fifty; the light-shaft pass's fixed
sample count (CNA-F13). Each was measured from here and written down. None
was worked around with anything that would have made the application
worse: no material cache that saves a tenth of a millisecond, no merging of
skinned figures into one mesh, no CPU skinning.

Also not touched, by the brief's own rule: more props, more car models,
more post-process passes, new shader features, faces beyond what a street
distance needs. The bolted wheel parts are still a draw each inside 32 m
(folding them into the body is a Blender re-export that risks the wheel
classification the seventh pass fixed, for four to eight draws on the one
or two cars in that range).

And one thing stopped half-way on purpose. Merging a parked car's parts by
material -- nineteen draws a copy on the Punto for a car nothing moves on --
is written and works, and merges nothing, because CNA's compiled-model
loader gives every mesh part texture objects of its own (CNA-F20) and no two
parts compare equal. The way round it would be to hash every texture's
pixels at load to recover the identity the loader discarded. That is the
kind of workaround the brief says to stop at, so the merge logs once per
model that it found nothing to merge, and waits.

## 3. The district, finished

The seventh pass left the district's street-facing blocks as one building
each: one render, one frame colour, one storey count for twenty-five metres
of street, a flat cross for a window frame, a blank wall wherever a cross
street opened a view of the block's end. From forty metres it read; from
twenty it said where the modelling stopped, and `comparisons.md` has the
frame that said it (`district-approach-10-before`).

Each block is now a terrace of *plots*, eight to fourteen metres wide,
and each plot is a building: its own render or brick, its own trim, frame
and door colour, its own storey count within one of its neighbours', its
own roof -- pitched with the ridge along the street and a stack, or flat
with plant -- so the party walls show above lower neighbours and the
roofline steps. Every window is a recess with reveals in the wall, a room
cell and a pane behind a framed sash with a mullion and a transom, a
projecting sill and a head. The rendered plots carry shutters folded back
beside their windows, a string course over the first floor, a balcony or
two on the middle bays with a slab, rails and uprights, and quoins up the
block's corners; the joints between plots carry a pilaster. A plot with no
shop has a door in a recess with a frame and a threshold, and ground-floor
windows either side. And the plot at a block's end, where a cross street
opens between every third block, has that end built as a second elevation
with windows and a door, because a building corner with one blank side is
a box.

What it deliberately does not have is the hero corridor's cost: no dressed
rooms behind the glass, no bevelled arrises, no weathering decals, no
fittings on the wall, no keystones. Every feature was chosen for what it
buys at twenty to sixty metres -- silhouette, parallax, a line of shade --
and a plot's window is nineteen quads where a hero window is about sixty.

Behind the windowed rows, the painted rows the seventh pass added carry a
cornice band at the eaves and, on the first row, a pilaster every nine
metres of a long face, so a printed elevation seen through a cross-street
gap has an edge and a rhythm rather than one plane. The skyline scatter
beyond is untouched: at 240 m relief is not a thing the eye can be sold.

The other half of the change is invisible: the district is batched a
*strip* of street at a time -- one cell per arm, per side, per ninety-five
metres -- rather than a 34 m cell per block, and the ground plane batches
at 230 m rather than 152. §6 has what that saved.

## 4. Near, mid, far

* **Near, the hero corridor to about 80 m** -- unchanged from the seventh
  pass: the modelled frontage with real reveals, dressed windows, bevelled
  sills and cornices, balconies with cast-iron balustrades, quoins,
  weathering, fittings, shop interiors that are rooms; scanned trees at
  their near level of detail; authored cars with rolling wheels and a
  person at each wheel; the crowd at full parts inside 20 m.
* **Mid, the district's windowed rows, from the end of the modelled street
  at 130 m to 316 m down the main street and 67 m to 198 m down the side
  street** -- the plots above: real recesses, framed sashes, sills, heads,
  shutters, string courses, balconies, quoins, doors, windowed ends;
  simplified rather than absent. About a tenth of a hero plot's triangles
  per plot. Casts shadows inside 90 m.
* **Far, the painted rows behind the frontage and the skyline** -- massing
  with a storey per texture tile, roofs with stacks and plant, a cornice
  band and pilasters on the first row; casts inside 60 m; the skyline
  scatter casts nothing and carries nothing.

Each tier is about an order of magnitude cheaper than the one in front of
it, and the ratio is what makes the transition gradual: hero to mid is a
change of density, mid to far a change of kind that happens behind a row of
mid-tier buildings where the eye is not looking.

## 5. The approach test

The cameras and the frames are in `comparisons.md`. Walking north along
the west footway toward the district:

| distance to the first district plot | what gives the district away |
|---|---|
| 60 m | nothing; the frontage fills the frame and the district is a strip between the trees |
| 30 m | before: one cream slab with a row of identical windows. After: a terrace -- terracotta with balconies, buff brick, cream -- of different heights. Nothing gives it away at the size it is drawn |
| 10 m | before: flat crosses for frames, no lintels, one building per block, a blank end wall at the corner. After: framed sashes, sills, heads, shutters, a windowed corner. The transition from the last modelled shop to the first plot is a change of detail density |
| standing among the plots, 5 - 15 m | the plots reveal what they are *not* -- no rooms behind the glass, no arrises on the sills, no weathering, the shopfront glazing one pane -- but not that they are a different generation of architecture |
| across the street, 20 m | the best single frame for the mid tier; nothing reads as cheaper than the frontage would at the same distance except the flat shutters and the undressed windows |

So the practical transition distances are: the mid tier holds to about
fifteen metres and is faultless past thirty; it used to hold to about forty
and be obvious at twenty. The far tier is never approached closer than the
service lane behind a mid-tier block, about twenty metres, and from a
cross-street gap at forty it now shows a cornice and pilasters where it
showed a plane.

## 6. The proving frame

`comparisons.md`: **`district-across`** for the mid tier at twenty metres,
and **`district-approach-10`** for the transition from the hero corridor.
For city depth, `district-behind` and `viewpoint-6` show the roofscape from
the frontage to the skyline as the seventh pass left it, with the painted
rows' new relief.

## 7. Where the frame went, and where it goes now

`performance.md` has every table and every caveat; the caveat that matters
most is that the machine's other sessions kept the load average between 5
and 37 during this session, and on this APU that moves the GPU clock as
well as the wall clock. The counts below are exact; the times are ranges.

Flagship view, Radeon 780M, 1600 x 900, session start against session end:

| | start (`9ac6ba2`) | end |
|---|---:|---:|
| draw calls | 1 443 | **1 212** |
| shadow draw calls | 1 899 | **932** |
| triangles drawn | 7.57 M | 6.79 M |
| shadow triangles, all cascades | 5.90 M | **2.22 M** |
| far cascade | 916 draws / 2.58 M | **378 / 0.83 M** |
| effect applies (opaque + transparent) | 1 285 | 1 057 |
| this side's setters + `Apply` | -- | 0.7 - 1.0 ms (0.7 - 0.8 us each) |
| the framework's draws | 34.8 ms at 27 us (measured mid-session) | 23.4 - 28.5 ms at 22 - 27 us |
| GPU shadow | 10.4 - 11.5 ms | 6.4 - 6.8 |
| GPU opaque | 24.1 - 25.4 | 14.1 - 17.6 |
| GPU post | 8.7 - 10.2 | 5.5 - 6.8 |
| GPU stage sum | 46.5 - 50.6 | 28.3 - 33.9 |
| wall clock, mean | 104.8 - 124.3 ms (load 10 - 14) | 40.2 ms at load 9 (24.9 fps); 50.3 - 67.2 at load 11 - 38 |
| static batches / mesh memory | 1 817 / 95 MiB | 1 507 / 106 MiB |

The shadow pass is the clean win: half the draws, a third of the far
cascade, no pixel moved. The opaque pass lost 230 draws and a tenth of its
triangles. The post chain renders the same pixels on both sides and reads
faster only because the GPU clock does; §8 has its breakdown measured
properly, variant against variant in one session.

## 8. The post chain, taken apart

The pipeline's own per-pass timers, on the `post` preset (405 draws; the
frame is mostly the chain), each variant a separate run of the same
benchmark. The GPU clock moved between runs with the load, so beside each
raw number is the pass as a multiple of the tone-map pass -- a full-screen
pass whose work never changes and so a clock the variants share:

| variant | post total | SSAO | light shafts | bloom | height fog | tone map | FXAA | SSAO / tone map | shafts / tone map |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| as shipped (16 samples, 4 bloom levels, shafts on) | 6.30 ms | 2.51 | 2.09 | 0.80 | 0.35 | 0.33 | 0.23 | 7.6 | 6.3 |
| `--ssao-samples 8` | 7.01 | 2.15 | 2.85 | 0.93 | 0.40 | 0.38 | 0.30 | **5.7** | 7.5 |
| `--ssao-samples 12` | 6.32 | 2.23 | 2.33 | 0.82 | 0.36 | 0.34 | 0.24 | **6.6** | 6.9 |
| `--bloom-iterations 3` | 6.52 | 2.71 | 2.09 | 0.80 | 0.36 | 0.34 | 0.23 | 8.0 | 6.1 |
| `--bloom-iterations 2` | 6.24 | 2.42 | 2.18 | 0.74 | 0.35 | 0.33 | 0.23 | 7.3 | 6.6 |
| `--no-light-shafts` | **4.61** | 2.83 | -- | 0.83 | 0.35 | 0.34 | 0.27 | 8.3 | -- |
| as shipped again, as a control | 5.67 | 2.12 | 1.88 | 0.78 | 0.35 | 0.34 | 0.21 | 6.2 | 5.5 |

The same seven runs on the `baseline` preset (1 212 draws; the chain is a
fifth of the frame):

| variant | post total | SSAO | light shafts | bloom | height fog | tone map | FXAA | SSAO / tone map | shafts / tone map |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| as shipped | 6.52 ms | 2.30 | 2.36 | 0.82 | 0.40 | 0.38 | 0.26 | 6.1 | 6.2 |
| `--ssao-samples 8` | 5.77 | 1.62 | 2.29 | 0.83 | 0.40 | 0.38 | 0.26 | **4.3** | 6.0 |
| `--ssao-samples 12` | 6.65 | 2.20 | 2.50 | 0.86 | 0.41 | 0.40 | 0.28 | **5.5** | 6.3 |
| `--bloom-iterations 3` | 6.92 | 2.51 | 2.50 | 0.83 | 0.40 | 0.39 | 0.29 | 6.4 | 6.4 |
| `--bloom-iterations 2` | 6.62 | 2.36 | 2.39 | 0.81 | 0.42 | 0.39 | 0.27 | 6.1 | 6.1 |
| `--no-light-shafts` | **4.13** | 2.26 | -- | 0.82 | 0.40 | 0.39 | 0.26 | 5.8 | -- |
| as shipped again, as a control | 6.89 | 2.49 | 2.52 | 0.82 | 0.40 | 0.38 | 0.28 | 6.6 | 6.6 |

What the two sets say:

* **SSAO is the most expensive pass** at 2.1 - 2.8 ms. On the `post`
  preset the two control runs read it at 2.51 and 2.12 and bracketed every
  sample-count variant; on the `baseline` preset the controls are tighter
  (6.1 and 6.6 tone-map units) and eight samples fall clearly below them at
  4.3 -- about two thirds of the cost, **0.7 ms** of a 30 ms GPU frame --
  with twelve at 5.5, borderline. So halving the samples is worth about
  two per cent of the frame. Not taken at High: the contact shadow under
  the kerbs and the sills is what SSAO is for here, and eight samples
  visibly speckle it.
* **The bloom pyramid's depth costs nothing measurable**: 0.80 - 0.83 ms
  at four levels, at three, at two, on both presets. The pyramid's lower
  levels are a handful of pixels. Not worth a dial.
* **Light shafts are the one large lever**: 2.1 - 2.5 ms, a third of the
  chain, seven per cent of the GPU frame, and the seventh pass called them
  the least visible of the six passes. They stay on at High because the brief says
  not to trade a visible feature for a small gain and this project's High
  is its look; they are already off at Medium and Low, and
  `--no-light-shafts` is there for a benchmark that wants the chain without
  them. The pass's sample count is not settable (CNA-F13), which is where
  a cheaper shaft would come from.
* Height fog, tone map and FXAA are 0.9 ms together and cannot be made
  cheaper from here.

So: the post chain is 5.5 - 7 ms of GPU at 1600 x 900 on this machine, two
thirds of it SSAO and light shafts, and the application-side optimisations
available through the pipeline's existing settings buy about 0.7 ms (SSAO
at eight samples) or 2.1 - 2.5 ms (shafts off), each at a visible cost.
Nothing is changed by default; the dials are measured and exposed, and a
`medium` machine already runs without both.

## 9. The benchmark

`--benchmark <preset>` is the seventh pass's `--frames` made reproducible:
a camera that is a constant rather than a viewpoint, a sun where the
workload wants one, twelve warm-up frames and sixty measured, the clock at
a fixed step so frame *N* holds the same traffic and crowd on every run,
overlay and sound and vsync off, and the result as one line of JSON (and a
CSV row with `--benchmark-output`). `scripts/benchmark.sh` runs the six
presets into one file per revision, with the GPU's own name from `glxinfo`
in a column the device cannot fill. `README.md` has the usage.

The six presets, final build, one run each, in the order they ran, from
the committed `benchmark/radeon-780m.json`; the load average is the
machine's, and at 6 - 9 this was the quietest half hour of the session:

| preset | load | CPU frame | GPU frame | shadow | prepass | sky | opaque | post | draws | shadow draws | skinned | people | triangles | shadow tris |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | 9.0 | 40.2 ms | 33.3 | 6.4 | 1.2 | 1.5 | 17.5 | 6.6 | 1 212 | 924 | 153 | 153 | 6.80 M | 2.21 M |
| shadow (sun at 28 deg) | 8.0 | 46.9 | 36.8 | 7.7 | 1.4 | 1.5 | 19.8 | 6.4 | 1 407 | 817 | 185 | 185 | 7.74 M | 2.80 M |
| traffic | 6.6 | 38.0 | 30.8 | 7.3 | 1.1 | 1.3 | 14.9 | 6.1 | 1 023 | 1 148 | 158 | 154 | 6.81 M | 3.21 M |
| crowd | 6.3 | 36.8 | 26.7 | 6.3 | 0.9 | 1.4 | 12.1 | 6.0 | 933 | 978 | 114 | 117 | 5.90 M | 2.88 M |
| city | 6.0 | 38.1 | 28.6 | 5.2 | 1.7 | 0.4 | 14.5 | 6.9 | 1 149 | 805 | 150 | 152 | 5.44 M | 2.15 M |
| post | 9.4 | 31.6 | 21.0 | 6.0 | 0.7 | 2.4 | 5.8 | 6.1 | 405 | 712 | 68 | 69 | 1.05 M | 2.03 M |

The same six presets run an hour earlier, under a load of 8 - 36, read 19 -
96 ms on the wall clock and 17.5 - 35.9 ms on the GPU with identical counts,
which is the whole argument for the load column.

Two things the table says that the flagship view alone did not. The
`traffic` preset, on the centre line among the moving cars, has the
*highest* shadow draw count of the six with the *lowest* draw count: the
near cars and their drivers are in every cascade their shadows cross,
which is the slice test doing its job in the other direction. And the
`post` preset -- 405 draws, a million triangles, the sky and the facades --
still costs 21 ms of GPU, of which 6.1 is the post chain and 6.0 the
shadow pass; that is the floor the frame sits on before any content is
drawn at all.

One result line, `baseline`, as `--benchmark` prints it (wrapped here;
`gpuPostPasses` and `cascades` are nested one level, everything else is
flat):

```
{"preset": "baseline", "what": "the representative view: the footway looking south
 to the junction, everything on", "version": "0.1.0", "renderer": "OPENGL33",
 "adapter": "Dell Inc. 27\"", "gpu": "AMD Radeon 780M (radeonsi, phoenix, LLVM 19.1.7,
 DRM 3.61, 6.12.107+deb13-amd64)", "content": "compiled", "width": 1600, "height": 900,
 "seed": 20260903, "warmupFrames": 12, "measuredFrames": 60, "loadAverage": 9.03,
 "cpuMeanMs": 40.208, "cpuMedianMs": 39.449, "cpuP95Ms": 45.553, "cpuMinMs": 34.818,
 "cpuMaxMs": 62.921, "fps": 24.87, "cullMs": 0.569, "shadowMs": 6.931, "prepassMs": 4.176,
 "skyMs": 0.121, "opaqueMs": 25.569, "postMs": 2.842, "opaqueApplyMs": 0.614,
 "opaqueDrawMs": 19.633, "skinnedMs": 5.283, "gpuFrameMs": 33.291, "gpuShadowMs": 6.432,
 "gpuPrepassMs": 1.236, "gpuSkyMs": 1.544, "gpuOpaqueMs": 17.496, "gpuPostMs": 6.583,
 "gpuPostPasses": {"SSAO": 2.341, "LightShafts": 2.387, "HeightFog": 0.395, "Bloom": 0.819,
 "Tonemap": 0.376, "FXAA": 0.264}, "draws": 1211.9, "shadowDraws": 924.2,
 "instancedDraws": 254.0, "skinnedDraws": 153.2, "triangles": 6802406,
 "shadowTriangles": 2213180, "materialApplies": 1058.7, "repeatedMaterialApplies": 67.0,
 "cascades": [{"split": 7.3, "draws": 113.4, "triangles": 379440}, {"split": 17.4,
 "draws": 136.5, "triangles": 345909}, {"split": 45.9, "draws": 296.8, "triangles": 653342},
 {"split": 190.0, "draws": 377.5, "triangles": 834488}], "visibleCharacters": 153.0,
 "vehicleDraws": 66.4, "driverDraws": 3.2, "characterShadowDraws": 9.0,
 "staticBatches": 1507, "instanceGroups": 342, "instances": 705, "meshBytes": 111595240,
 "textureBytes": 925869624}
```

(`textureBytes` is what `MaterialLibrary` counts -- every map it uploaded
with its mip chain, the imported models' included -- and is the overlay's
number, not a measurement of GPU memory.)

The committed baseline for future comparisons is
`docs/visual-overhaul-8/benchmark/` -- the CSV and the JSON lines of the
six presets on the final build -- against which a CNA change can be run
with one `scripts/benchmark.sh` and one diff.

## 10. Validation

**Tests.** CTest 16 of 16 on the final build: the fourteen suites of the
seventh pass, `shadow_cull_tests` (18 checks: the bollard, the person, the
leaning building, the caster behind the camera, the sun on the horizon,
depth against distance) and `benchmark_tests` (67 checks: the preset table,
the JSON read back through System.Text.Json, the CSV against its own
header). `validate-assets.py` and `vehicle-orientation.py` pass unchanged.
`git diff --check` is clean.

**The walkthrough**, on the final build, against the seventh pass's table:

| leg | asked | moved | blocked | closest to a car | inside one | pushed |
|---|---:|---:|---:|---:|---:|---:|
| down the footway | 29.1 m | 28.5 m | 26 steps | 2.08 m | 0 | 0 |
| into a parked car | 8.8 m | 3.5 m | 207 steps | **0.32 m** | **0** | 0 |
| along the kerb past them | 37.8 m | 22.1 m | 667 steps | 0.34 m | 0 | 0 |
| standing in a traffic lane | 0 | 2.0 m | 0 | 0.00 m | 37 | 2.0 m |
| watching the traffic | 0 | 0 | 0 | 3.14 m | 0 | 0 |
| at the crossing | 0 | 0 | 0 | 6.33 m | 0 | 0 |

Every number is the seventh pass's to the centimetre, and the worst
disagreement between a moving car's drawn heading and its direction of
travel over 168 445 samples is the same **2.88 degrees**. The parked cars
are still solid at their real boxes, a car that drives into the camera
still gives it back, nothing walks through anything.

**The lineup** (`--lineup --capture`, 86 frames): every authored car in its
row at its near level of detail with its wheels straight; every driver's
window shows a figure on the driver's side with both hands on the wheel.
`build-probe` holds the frames; they are not committed, since nothing in
them changed from the seventh pass but the parked copies' grouping.

**Screenshots.** The eighteen named viewpoints captured by the final build
against the same capture from a copy of the HEAD binary, pixel for pixel:
seventeen differ by 0.03 - 0.55 per cent of their pixels -- the district at
the vanishing point, the hydrant's shadow, parked cars beyond 45 m drawn
from their far copies -- and `06-above-the-junction` by 27 per cent, which
is the view that looks down onto the district's roofs and the district is
rebuilt. The committed `docs/screenshots` set is regenerated from the final
build, as the seventh pass's was: a new baseline, not a comparison (it was
also one commit stale, predating the wheel-axle fix, which the eighth pass's
shadow validation found and §1 of `performance.md` records).

**Git.** `cna-street` is clean at the end of the session. The siblings:
`../cna` on `develop` at `1bb2145d9` and `../cnanext` on `next`, which
another agent moved from `6e22a44b6` through `4bf74e31b` to `d42203805`
during this session, both with that agent's working-tree edits to their
planning documents (`NEXT.md`, `NEXT_gltf.md`, `integration/*.md`,
`.gitignore`), present before this session began and not touched by it;
`../sharp-runtime` `develop` `df1b42ab` and `../sharp-runtimenext` `next`
`30ccdef3`, clean; `../easy-gl` `develop` `deda7a4` and `../meta-gl`
`develop` `20c8b2d`, each with a `VERSION` file that predates this session.
No command that writes was run in any of them.

**Commits**, in order: `11f9969` perf(shadows): a caster is written only
into the cascades its shadow can reach, and the draw cost is timed in two
halves; `8e3cdeb` feat(district): the mid tier -- each block a terrace of
plots, batched a strip at a time; `8b45cd0` perf(vehicles): a level of
detail per parked car, from two groups either side of one distance;
`6bcc567` feat(bench): --benchmark presets with a machine-readable result,
and the post chain's two dials exposed; `980a8c1` feat(bench): the GPU's
own name in the result, from the environment; and the documentation commit
that carries this file, the regenerated screenshot set, the before/after
pairs and the benchmark baseline.

---

## The questions, answered

1. **What was still worth doing on this side.** The district's facades up
   close (the seventh pass's own first weakness), the far cascade (its
   second), the parked cars' four rings and their never-reached far copies,
   the skyline's one-cell-per-block batching, and the unmeasured split of
   the opaque pass between this side and the framework. All five were
   application work and all five were done.
2. **Deliberately not touched.** `PbrEffect`, `BindDrawParams` and the draw
   path (CNA-F19); the caster program (CNA-F6); the skinned effect's bone
   palette (CNA-F14's shape); the light-shaft sample count (CNA-F13); the
   loader's per-part textures (CNA-F20). Each measured from here, none
   worked around with anything that would have made the application worse.
3. **What changed in the district.** §3: blocks became terraces of plots
   with their own render, frame, door, storeys and roof; windows gained
   reveals, framed sashes, sills and heads; rendered plots gained shutters,
   string courses, balconies and quoins; plots without shops gained doors;
   ends facing cross streets became elevations; the painted rows gained a
   cornice band and pilasters. Batched a strip at a time.
4. **Near, mid, far.** §4: the modelled hero corridor to 80 m; the
   windowed plots from the end of the modelled street to 316 m (main) and
   198 m (side), casting inside 90 m; the painted rows and skyline beyond,
   casting inside 60 m and not at all. Each tier about an order of
   magnitude cheaper than the one in front of it.
5. **Transition distances.** §5: the mid tier is faultless past thirty
   metres and holds to about fifteen; it used to hold to about forty and be
   obvious at twenty. The far tier is never approached closer than the
   service lane, about twenty metres, and at forty shows relief where it
   showed a plane.
6. **The proving frame.** `pairs/district-across-before.png` against
   `-after.png`, twenty metres across the street at the opposite row; and
   `pairs/district-approach-10-*` for the transition from the modelled
   frontage.
7. **Draw calls.** Flagship 1 443 to 1 212; above the junction 1 385 - 1 401
   to 1 158; the long view 1 590 to 1 407.
8. **`PbrEffect::Apply`.** Measured for the first time: this side's ~25
   setters plus `Apply` are 0.7 - 1.0 ms a frame, 0.7 - 0.8 us a call, for
   1 057 calls on the flagship view (1 285 before the consolidation), of
   which 67 (134 before) repeat the previous material and environment. The
   framework's draws are 23 - 35 ms at 22 - 27 us each. A state cache on
   this side would save about 0.1 ms.
9. **Batching and consolidation performed.** District strips (one cell per
   arm, side and 95 m instead of one per block), skyline sectors (twelve
   instead of ninety cells), the ground plane at 230 m, and the parked
   cars as two per-instance groups instead of four rings; the parked-copy
   material merge written and dormant on CNA-F20.
10. **Shadow draws.** 1 899 to 932 on the flagship view; 1 103 - 1 471 to
    802 above the junction; 1 449 to 807 on the long view.
11. **Shadow triangles.** 5.90 M to 2.22 M; 3.4 - 4.3 M to 2.13 M;
    6.24 M to 2.81 M.
12. **Shadow GPU time.** 10.4 - 11.5 ms to 6.4 - 6.8 on the flagship view;
    6.1 - 7.5 to 5.1 above; 10.1 - 11.3 to 7.5 - 7.8 on the long view --
    with the load caveat of §7.
13. **Per cascade, flagship.** 151 / 277 / 555 / 916 draws and
    0.58 / 0.96 / 1.77 / 2.58 M triangles to 114 / 143 / 297 / 378 draws
    and 0.39 / 0.37 / 0.66 / 0.83 M.
14. **What still dominates the far cascade.** Content between 46 and 190 m
    whose shadow is on screen: the three tree species from their far level
    of detail (0.68 M triangles over 34 draws), the frontage's window
    frames (0.19 M), its ashlar and zinc (0.29 M), the parked cars' far
    copies (0.1 M each). A caster mesh for the frontage without reveals and
    frames is the next content step; an instanced caster program is CNA-F6.
15. **Post-process breakdown.** §8.
16. **Most expensive post stage.** §8.
17. **Did a safe post optimisation help.** §8.
18. **Vehicle draw structure.** `performance.md`, the eight-car table:
    near parts 7 - 19, far 3 - 5, moving inside 32 m body + rolling and
    bolted wheel parts, parked inside 45 m the near parts, beyond it the far
    copy, the far group casting for every copy.
19. **Character and driver cost.** 153 - 184 skinned draws a frame, three
    a figure, 556 - 749 k triangles; drivers 0 - 5 draws (inside 30 m, moving
    cars only); shadow proxies 10 - 29 draws (were 70 - 110).
20. **Blocked on CNA.** The per-draw cost (CNA-F19); the parked-copy merge
    (CNA-F20); instanced shadow casting (CNA-F6); a skinned caster and an
    instanceable bone palette (CNA-F14); the light-shaft sample count
    (CNA-F13).
21. **CNA findings added or materially updated.** CNA-F19 (new), CNA-F20
    (new); CNA-F6 updated with this pass's numbers in the follow-up
    document; three behaviours recorded under the eighth pass's notes.
22. **Evidence per finding.** CNA-F19: the two-halves timing on three
    views, and the reading of `BindDrawParams`; CNA-F20: the material dump
    of the Punto's nineteen parts -- distinct texture objects with empty
    names -- and the reading of `BuildPartEffectEXT`.
23. **CNA untouched.** Confirmed; §10 has the sibling status.
24. **Benchmark presets.** §9: baseline, shadow, traffic, crowd, city,
    post, each a fixed camera, sun and window at a fixed clock step.
25. **Example machine-readable result.** §9.
26. **Radeon 780M final measurements.** §7 and §9.
27. **Three largest remaining visual weaknesses.** Faces at
    conversational distance; the parked cars' near copies still eight to
    nineteen draws and the moving cars' bolted parts; static reflections
    (a moving car is not in a shop window).
28. **Three largest remaining performance limitations.** The per-draw
    cost of the stock-effect draw path (CNA-F19); the skinned crowd at
    three uninstanceable draws a figure; the shadow pass's one draw per
    caster (CNA-F6).
29. **Whose they are.** All three are CNA's. On this side what remains is
    content: a shadow caster mesh for the frontage, the bolted wheel parts
    folded into the bodies in Blender.
30. **Live-play regressions.** §10: none.
31. **Validation results.** §10.
32. **Git status.** §10.
33. **Commits.** §10.
34. **Is there substantial cna-street work left, or should performance
    work move into CNA.** It should move. The frame on this machine is
    1 200 draws at 22 - 27 us of driver time each plus 150 skinned draws
    that cannot be fewer plus 900 shadow draws that cannot be instanced,
    and every one of those numbers is set by the framework. What this side
    can still do is content -- a caster mesh for the frontage, the bolted
    wheel parts, faces -- and none of it moves the frame by more than a few
    per cent. The benchmark is the handover: six presets, one line each,
    and the load average beside every number.

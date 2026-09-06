# The seventh pass: city depth, and where the frame goes

The brief was two things that turned out to be one. Make the city hold up
when the player looks and walks past the hero block; and stop guessing
where the remaining forty milliseconds go, measure it, and attack the
largest real costs. It became one thing because the measurement said
where the waste was, and the waste was in exactly the content that made
the city deep -- the trees, the parked fleet, the district -- so the same
work paid for both.

Before either, four defects were reported from the keyboard by somebody
driving the street, and one measurement error was found underneath three
of them. They come first, because a city street on which every driver
sits in the passenger seat is not a city street however far it goes.

Baselines, recorded before anything changed:

| Repository | Baseline | Note |
| --- | --- | --- |
| `cna-street` | `9cec90d` on `develop` | The commit this pass starts from |
| `cna` (`../cnanext`) | `next` | Read-only; another agent moved it from `3faa193` to `5c88406` during this session |
| `sharp-runtime` (`../sharp-runtimenext`) | `next` `30ccdef3` | Read-only |
| `easy-gl` | `develop` `deda7a4` | Read-only; an untracked `VERSION` predates this session |
| `meta-gl` | `develop` `20c8b2d` | Read-only; likewise |

**No framework repository was modified by this session.** Every change in
this pass is in `cna-street`. The only commands run in any sibling were
read-only (`git status`, `git log`, `git rev-parse`, and reads of source
files). At the end of the session `../cna` and `../cnanext` both showed
working-tree edits to their planning documents (`NEXT.md`,
`integration/*.md`, `plans/plan_binding.md` and the like) that were not
there at its start; those are the other agent's, working in those trees
concurrently, and are recorded here so that nobody attributes them to this
one. One new CNA limitation was found and is recorded as CNA-F18 in
`docs/cna-findings.md`; three behaviours worth knowing are recorded under
the seventh pass's notes there.

---

## 1. The four defects, and the cube underneath them

**The street drove on the left.** A vehicle keeps to its own right, and
which world direction that is follows from the frame and not from a
compass. This project's own `Geometry::AlongFrame` had said since the
first pass that the left of a heading (ux,uz) is (uz,-ux), so the left of
a car facing +Z is +X -- and the northbound lane was at +X. The street
was laid out for left-hand traffic under a comment saying right-hand,
with eight left-hand-drive car models on it. The lanes, the parking bays
and the four signal heads all keep right now, through one
`TrafficSystem::rightOf`, and `traffic_system_tests` checks the rule
rather than four hand-written signs.

**Nobody was driving.** `driverSeat` put the figure at -X on the strength
of a comment, which is the passenger seat. `scripts/vehicle-orientation.py`
now measures which side of the centre line the steering wheel is on --
the triangle area either side of the dashboard slab, the inner two-thirds
of the cabin only, so the symmetric doors and mirrors do not bury it --
and six of the eight models come back left-hand drive, two carry too
little cabin to say, and none comes back right. `CityScene::driverSeat`
takes that side. `lineup/driver-before.png` is the wheel with nobody
behind it; `lineup/driver-after.png` is a person.

**The driver was a mannequin.** A rigid two-draw prop: an ellipsoid with no
face, a bar for shoulders, spheres for hands. It is now one of the crowd's
own imported MakeHuman figures -- same mesh, same skin, same derived face
normals, same nineteen-bone skeleton -- playing a new `drive` clip:
seated, reclined a driver's slouch, both hands on a rim in front of the
chest, correcting the wheel and looking about on two beating frequencies
so no two cars hold the same pose. Three draws instead of two, inside
30 m, moving cars only, and no mesh memory at all. `gait_tests` checks a
driver's knees are forward of the hips, feet below the knees, hands on a
wheel a wheel's width apart, and head over the shoulders, on both rigs.

**The yellow car's wheels wobbled.** A wheel node as `blender-vehicles.py`
leaves it is everything the splitter found near that corner of the car,
and on five of the eight models that is more than the rim and the tyre:
the Mini's rear nodes carry an arch liner 20 cm above the axle, the small
price car's -- the yellow one -- a mudflap 25 cm above it, the Logan's a
suspension arm, the VAZ's a caliper. Rolled with the wheel, every one of
them orbits the axle once a revolution. A part now rolls only if it is a
surface of revolution about the axle -- centred on it and as tall as it
is long -- and the rest turn with the stub axle and stand still. The
rolling radius is now the rolling part's, so the odometer no longer
turns a tyre by a mudflap's height.

**Every imported model's box was a cube.** Underneath the driver sitting
on the bonnet: CNA's `ModelMesh` publishes a bounding sphere and no box,
and `ModelLibrary` took a cube of side 2r round it. The Astra reported
itself as 4.47 x 4.47 x 4.47 m; measured from its vertices it is
4.47 x 2.02 x 1.51. That cube was the walker's collision solid, the
model's cull bounds, and the box `fitTo` scaled every prop to a real-world
height from. `VertexBuffer` keeps a CPU shadow of what was written into it
and `GetDataRawEXT` hands a window of it back, so a part's box is now
measured from its positions at load -- a memcpy, not a read-back. CNA-F18.

And the window is no longer black for the twenty-five seconds the city
takes to build: the overlay is created before the scene, every stage
reports what it is doing, and the probe bake reports per probe.

## 2. Where the frame goes

`performance.md` has the tables. The short version, on the Radeon 780M at
1600 x 900, flagship view:

| stage | CPU | GPU |
|---|---:|---:|
| shadow | 16.0 ms | 9.8 ms |
| prepass | 5.8 | 1.2 |
| sky | 0.1 | 1.7 |
| opaque | 32.1 | 20.0 |
| post | 3.7 | 7.4 |

**The opaque pass is CPU-bound by twelve milliseconds of `PbrEffect::Apply`**
-- 1 436 draws at about 22 us of driver time each -- and the post chain is
GPU-bound at seven, of which SSAO and light shafts are two and a half
each. The sixth pass diagnosed submission from the CPU numbers alone;
this is the first time the GPU has been asked, and it agrees. The
shadow pass, before this pass's work, was 13.2 M triangles of which the
three scanned tree species were 7.3 M: a canopy silhouette that a
cascade texel metres wide cannot resolve, drawn to the leaf.

What was done about it, in order of what it bought: trees and props cast
from their far level of detail (13.2 M to 8.4 M shadow triangles, GPU
shadow 15.5 to 9.8 ms); the parked fleet casts from a shadow-only proxy
(8.4 M to 6.5 M); architecture, the district and people each got a shadow
distance of their own (2 479 shadow draws to 1 892); and the instanced
renderer keeps its buffer between frames instead of allocating one per
group per frame. No visual difference was found in any of it, at the
flagship, long-view, car-close and crossing viewpoints.

The frame is not much shorter on the wall clock -- 50.1 ms at the start of
the session, 48.8 at the end, best of three under a load average this
machine's other sessions kept between six and twenty -- and that is
the honest reading: the pass added a city behind the frontage, thirty
skinned drivers, an audio mixer and a second row of cull work, and took
away half the shadow pass, and on this machine on this day the two
roughly cancel. On the GPU timers, which are read from the hardware and
do not care about the load, the shadow pass is 5 to 6 ms lighter and
nothing else moved. The sixth pass's own figure for the start-of-session
build was 59 - 63 ms on a quieter day; the same build measured 50 today,
and the cube fix above is the only rendering change between them.

## 3. The city behind the frontage

The sixth pass continued the street past the modelled block with rows of
windowed buildings down both arms, and from the footway it reads. What it
never took was the view from above, or from any window past the second
floor, or leaning back on the crossing: **one row of buildings, then a
checkerboard of grass and dirt to the horizon**, with a haze of skyline
blocks 240 m out. The project's own `06-above-the-junction` flagship
screenshot had had that field in it since the first pass.
`pairs/district-behind-the-frontage-before.png` is the clearest.

Two more rows of blocks behind the street-facing one, down both arms of
both streets: `paintedBlock`, no window geometry, because nobody ever
stands close enough to one of these to ask for a real opening; a service
lane's width between rows so the massing reads as blocks and not a slab;
bigger and plainer in the second row so the skyline scatter beyond it is
not a step up in both height and density at once; the same pitched roofs
with stacks and flat roofs with plant the frontage has. About 5 500
triangles for the lot.

What it costs is draw calls, and the first cut cost too many: one 34 m
cull cell per block was +156 draws on the flagship view for buildings the
frontage hides. Batched a whole strip at a time -- both rows in one 150 m
cell per material, each cell drawing on three of the six facades -- it is
+45, and the rows cast shadows only inside 60 m, since from the street
they stand behind a row of buildings and from above the near ones still
shade their neighbours' roofs.

The tiers, as they stand:

* **Near, the hero corridor to about 80 m**: the modelled frontage with
  real reveals, balconies, shutters, quoins and weathering; scanned trees
  at their near level of detail; authored cars with rolling wheels and a
  person at each wheel; the crowd at full parts inside 20 m.
* **Mid, 80 to about 200 m**: the windowed district blocks with real
  window recesses, shopfronts, plinths and cornices; the same trees and
  cars at their far level of detail; the crowd at collapsed parts, no
  shadow past 38 m.
* **Far, behind and beyond**: the painted rows, the skyline scatter, the
  end-caps that close each arm.

What still gives the district away is recorded under §6.

## 4. Sound

The demo drew for six passes and said nothing. `SoundScape` is the sound
of the street through CNA's own XNA audio -- a `SoundEffect` per sample,
a `SoundEffectInstance` per voice, `Apply3D` against an `AudioListener` at
the camera -- so every placed source gets its distance attenuation, its
pan across the listener's own right axis and its Doppler shift from the
mixer.

One looping engine for each of the nearest six moving cars, its sample
picked by speed (idle, low revs, high revs, on the move), its pitch bent
inside the band by the speed and by a detune of the car's own, its
emitter carrying the car's velocity so a car passing the camera drops as
it goes; a horn, rarely, from a car braking hard nearby; the walking
camera's own footsteps a stride apart and the nearest pedestrians' from
their own feet on the same clock their legs run on; a laugh from somebody
waiting at a crossing, now and then; wind everywhere and unplaced; birds
in whichever tree is nearest. Twenty-two samples, all from NOX Sound's
Essentials packs under CC0, derived by `scripts/prepare-audio.py` and
declared in the manifest as a sound set acquired by hand, since the
packs are a download behind a form and not a URL.

## 5. Driven, not photographed

`--walkthrough` on the final build, through the same `moveWithCollision`
and `escapeSolids` a person's WASD goes through, at a fixed step:

| leg | asked | moved | blocked | closest to a car | inside one | pushed |
|---|---:|---:|---:|---:|---:|---:|
| down the footway | 29.1 m | 28.5 m | 26 steps | 2.08 m | 0 | 0 |
| into a parked car | 8.8 m | 3.5 m | 207 steps | **0.32 m** | **0** | 0 |
| along the kerb past them | 37.8 m | 22.1 m | 667 steps | 0.34 m | 0 | 0 |
| standing in a traffic lane | 0 | 2.0 m | 0 | 0.00 m | 37 | 2.0 m |
| watching the traffic | 0 | 0 | 0 | 3.14 m | 0 | 0 |
| at the crossing | 0 | 0 | 0 | 6.33 m | 0 | 0 |

Worst disagreement between a moving car's drawn heading and its direction
of travel over 168 445 samples: **2.88 degrees**, the Bezier tangent
sampling on the junction, exactly the sixth pass's figure. Two rows moved,
both because the cars' collision solids are now their real boxes rather
than cubes of their diagonal: the kerb leg walks 22 m of the row where it
walked 12.5 m against invisible walls two metres out from every car, and
the traffic-lane leg is pushed 2 m rather than 12 because a car that
drives into the camera is a car's width and not a cube's. The six live-play invariants of
the sixth pass -- forward-facing traffic, plausible legs, uncollapsed
crowds, collision with cars and furniture, visible drivers, no lofts in
the hero bays -- are all still checked, by `traffic_system_tests`,
`gait_tests`, `pedestrian_tests`, `vehicle_orientation` and the
walkthrough, and all pass. Two of those suites grew this pass: the
traffic suite checks the side of the road as a rule, and the gait suite
checks the seated pose.

The application was run in a window on the machine's own display and
its own GPU with sound on; the `--frames` profiles above were taken from
that window. Xwayland still refuses `xdotool` its focus, so every
interactive claim is made from `--walkthrough` and the suites, as before.

## 6. Tried, rejected, and still wrong

- **Material state caching in `applyMaterial`.** `PbrEffect::Apply`
  uploads its whole parameter block whether or not anything changed, so
  skipping the setters saves the setters and not the upload. Not done.
  This is the boundary the opaque pass sits against.
- **A flat margin on the far cascade's reach.** Same finding as the sixth
  pass.
- **One cull cell per infill block.** +156 draws for hidden buildings;
  batched per strip instead.
- **Committing the sounds.** Ten megabytes of CC0 WAV in the repository
  would have been the easy path; they are derived and gitignored like
  everything else third-party, and the manifest says how to get them.

What still gives it away:

1. **The district's facades, up close.** Real openings, but one plane of
   render with a shopfront band, and the rows behind are painted. A third
   tier -- balconies, quoins and shutters on the windowed blocks, a
   windowed first row behind the frontage -- is the next step.
2. **The far cascade.** Half the shadow pass, because its fit sphere
   contains most of the near street. A reach test against shadow extent
   rather than distance from the slice's centre is the next shadow win;
   and the hydrant is 6 200 triangles a copy.
3. **A hundred and fifty skinned draws.** Unchanged and unchangeable on
   this side: a palette per figure (CNA-F6's shape).

---

## The questions, answered

1. **The cliff at session start.** Not a facade cliff -- the sixth pass's
   windowed district reads from the footway -- but a *depth* cliff: one row
   of buildings deep, then open ground to a skyline 240 m out, visible from
   any elevated or tilted view including the flagship `06-above-the-junction`.
2. **Mid-distance architecture.** Unchanged in itself; what changed is what
   stands behind it (§3) and that it casts shadows to 150 m rather than
   190.
3. **Far architecture.** Two rows of painted blocks with roofs, stacks and
   plant behind the frontage on both arms of both streets, thinning into
   the existing skyline scatter.
4. **Roofs, skyline, termination.** A continuous roofscape from the
   frontage to the skyline in every elevated view; the end-caps and the
   skyline scatter are as they were.
5. **Distant vegetation.** Unchanged in what is drawn; every tree now
   *casts* from its far level of detail at any distance.
6. **Distant vehicles and pedestrians.** Cars past 32 m draw and cast from
   their far copy; parked cars cast from it at any distance. People cast
   no shadow past 38 m; their draw structure is unchanged (3 draws at any
   distance, CNA-F6).
7. **The proving frame.** `pairs/district-behind-the-frontage-before.png`
   against `-after.png`; and `before/06-above-the-junction.png` against
   `after/`.
8. **The breakdown.** §2 and `performance.md`: CPU cull 0.6, shadow 16.0,
   prepass 5.8, sky 0.1, opaque 32.1, post 3.7 = 58.4 ms of submission;
   GPU shadow 9.8, prepass 1.2, sky 1.7, opaque 20.0, post 7.4 = 40.1 ms of
   execution, overlapped; 48.8 ms best-of-three wall clock.
9. **CPU vs GPU.** Shadow and opaque are CPU-bound (16 vs 9.8, 32 vs 20);
   post and sky are GPU-bound (3.7 vs 7.4, 0.1 vs 1.7); prepass is
   CPU-bound five to one. The GPU is busy 69 - 77 per cent of the wall
   clock across the three viewpoints.
10. **Five most expensive stages.** Opaque (20.0 GPU / 32.1 CPU), shadow
    (9.8 / 16.0), post (7.4), prepass (1.2 / 5.8), sky (1.7).
11. **What was optimised and why.** §2: shadow LOD for instanced props,
    shadow proxies for the parked fleet, three shadow-distance policies,
    the instanced buffer cache -- each because `shadowReport()` or the two
    clocks named it.
12. **Draw calls.** 1 405 to 1 436 (+45 for the district, -14 elsewhere).
13. **Shadow draws.** 2 530 to 1 892.
14. **Shadow pass.** GPU 14.6 - 15.8 ms to 9.3 - 9.8; CPU 15.3 - 16.1 to
    14.2 - 16.0; triangles 13.2 M to 6.5 M.
15. **Opaque pass.** GPU 14.6 - 19.0 to 17.0 - 20.0 (the district and the
    drivers); CPU 26.5 - 32 both ends, load-dominated.
16. **Character cost.** 153 draws / 558 k triangles unchanged; shadow
    proxies 272 draws to 77.
17. **Vehicle cost.** 65 draws / 378 k triangles in frame, unchanged; the
    bolted wheel parts add a draw each inside 32 m and the hero fleet's
    shadows dropped from ~1.2 M triangles to a few hundred thousand.
18. **Post chain.** SSAO 2.4 - 2.9, light shafts 2.1 - 2.7, bloom 0.8,
    height fog 0.4, tone map 0.4, FXAA 0.25 ms, GPU.
19. **Largest speedup.** Vegetation shadow LOD: 4.8 M triangles and 5 - 6 ms
    of GPU shadow time.
20. **Rejected.** §6.
21. **Per cascade.** `performance.md`: 152 / 276 / ~550 / ~1 030 draws and
    0.58 / 0.96 / 1.86 / 3.09 M triangles for cascades 0 - 3 on the
    flagship view.
22. **Per vehicle.** `performance.md`, the eight-car table.
23. **Pedestrian tiers.** Near (20 m): 3 draws full parts + proxy; mid
    (38 m): 3 draws collapsed + proxy; far (130 m): 3 draws collapsed, no
    proxy.
24. **Drivers.** They cost more, not less -- 3 skinned draws instead of 2
    rigid ones, inside 30 m -- and the cars no longer look empty *or*
    driven by mannequins. On the flagship view that is one car and 3
    draws. The glass-lighting workaround (a darker driver tint) no longer
    applies: the driver is the crowd's own skin, and behind the screen it
    reads as a person, which is what mattered.
25. **New CNA limitation.** CNA-F18: `ModelMesh` publishes a bounding
    sphere and no box. Quantified: 73 per cent too wide and too tall on
    every authored car. Worked around from the vertex buffer's CPU shadow.
    Also noted: a `GpuTimer` range cannot contain another (GL's one open
    `TIME_ELAPSED`); the mixer's simultaneous-voice ceiling is unpublished.
26. **CNA not modified.** Confirmed; §32 below.
27. **Real GPU before/after.** 1600 x 900: 50.05 to 48.75 ms best of three
    (20.0 to 20.5 fps); draws 1 405 to 1 436; shadow draws 2 530 to 1 892;
    triangles 7.50 M to 7.55 M; mesh memory 94 to 95 MiB.
28. **llvmpipe.** Not re-measured; the GPU timers now make the point the
    llvmpipe comparison used to.
29. **Three largest visual gaps.** The district's facades up close; faces
    at conversational distance; static reflections.
30. **Three largest bottlenecks.** `PbrEffect::Apply` per draw (12 ms of
    CPU in the opaque pass); the far shadow cascade (half the shadow
    pass); the skinned crowd (153 uninstanceable draws).
31. **Git status.** `cna-street` clean at the end of the session; siblings
    unchanged by this session (`cnanext` moved by another agent).
32. **Commits.** `37c0c24` fix(street): the traffic on the right, somebody
    behind the wheel, and wheels that stay round; `0134f06` perf(shadows):
    measured, not guessed; `453f8c3` feat(district): the city behind the
    frontage; `ce38178` feat(audio): the street makes a sound; `0a1b9d2`
    perf(frame): one instance buffer per mesh, kept; people cast only
    near; and the documentation commit that carries this file.
33. **Final checks.** CTest 14/14; `validate-assets.py` 69 models, 14
    surfaces, 1 sound set, 1 tool, 351 files verified, 0 problems;
    `check-screenshots.sh` 18 of 18 viewpoints within tolerance against the
    regenerated set (0.000 - 0.011 per cent of pixels differ between two
    runs of the same build, which is the fixed-step clock doing its job);
    `git diff --check` clean.

The regenerated screenshot set is a new baseline, not a comparison: every
one of the eighteen changed, because the traffic keeps right and the city
has a back to it. The five that show it best are in `before/` and
`after/`.

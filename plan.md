# plan

Where the project is. Updated as it moves.

## Completed

**Phase 1 — audit.** Both dependencies read rather than guessed at: CNA's
module layout, its public XNA surface, the whole `CNAEXT` graphics layer, the
`PbrEffect` contract, the capability model, the CNB content pipeline and its
tools, and sharp-runtime's 44 components. Written up in `docs/cna-audit.md`,
with the baselines in `dependencies.lock`.

**Phase 2 — the build and the loop.** Modern CMake consuming CNA as a sibling
checkout, `CNA_CNAEXT` forced on, warnings scoped to this project's targets,
Linux first class, no IDE dependency and no absolute paths. A `Game` subclass,
a free camera, a settings document and a screenshot path.

**Phase 3–4 — the street.** `StreetMetrics.hpp`, the dimension book everything
reads. `CityLayout` generating a crossroads with eight frontages and 42 plots
from one seed. `RoadBuilder`: carriageway, junction box with corner fillets,
kerbs with a dropped profile at the crossings, footways, every marking, the
ironwork. `BuildingBuilder`: six architectural families, real window openings
with reveals and interiors, shopfronts, balconies, cornices, four roof styles,
rainwater goods.

**Phase 5–6 — surfaces and light.** `TextureFactory`: 30 authored procedural
surfaces in linear light, tileable by construction, with normal and ORM maps
derived from one height field. `SignFactory` and an SDF canvas with a stroke
font. `MaterialLibrary` with the neutral-texture/tinted-material strategy and
full mip chains. `SkySystem` on CNA's analytic model, feeding
`EnvironmentProcessor` for image-based lighting. `SceneRenderer`: cascaded
shadows, a depth/normal prepass, the HDR pipeline and its post chain, sorted
opaque and transparent draws, and a capability probe in front of every optional
subsystem.

**Phase 7–10 — the things in it.** `PropFactory` (lighting, seating, bins,
hydrants, cabinets, bike stands, planters, a bus shelter, trees and their
grates, signal heads and posts and masts, signs, street plates),
`VehicleFactory` (a lofted hull with a greenhouse and glass, five classes),
`PedestrianFactory` (posed figures at eight phases of a stride). `TrafficSignals`
as an eight-stage fixed-time controller, `TrafficSystem` as a car-following
model, `PedestrianSystem` as a walk graph that obeys the green man. All of it
placed by `CityScene`, with the shop fascias and house numbers lettered from
the anchors the façade generator leaves behind.

**Phase 11–13 — the frame.** Instancing for every repeated prop, frustum and
distance culling per batch and per instance with a shorter leash for shadows,
a debug overlay whose stage breakdown sums to the frame it measures.

**Phase 14 — the content pipeline.** `bake-assets --content` writing the
catalogue's surfaces as PNG through a device-less bake mode, a CMake `content`
target compiling them with `cna_tool_source_to_cnb`, and
`ContentManager::Load<Texture2D>` at start-up with a silent fallback to
generating. It needed a mip-chain generator contributed to CNA to be worth
having.

**Phase 15 — tests.** Eight suites, registered with CTest, over the logic that
has no picture. Two found live bugs on their first run. Plus image-based
screenshot regression: a fixed-step capture clock makes two runs of an unchanged
build bit-identical, and `scripts/check-screenshots.sh` compares every named
viewpoint against the committed set.

**Phase 16 — documentation.** `README.md`, `docs/design-notes.md`,
`docs/cna-findings.md`, `assets/ATTRIBUTION.md`, and the screenshot set.

## Visual-quality work done

Each of these was found by looking at a screenshot, and each was fixed rather
than noted:

* Side-street frontages generated **inside** the corner blocks, which put a
  17 m blank flank wall on all four corners of the junction where four
  shopfronts should have been.
* Shop interiors drawn with the interior *atlas* — a 4×4 grid of whole rooms —
  stretched across the back wall, so every shopfront had a wall of thumbnail
  rooms behind the glass. And their back wall and ceiling culled, so a shopper
  looking in saw the sky through the back of the shop.
* Elevations that do not front a street built as blank walls, including the ends
  of terraces and the rear elevations visible from the junction.
* Vehicle hulls lofted inside out: every near-side panel culled, so a car looked
  like it was made of glass. Greenhouses whose glass was buried inside the
  pillar in the middle and floating in air at the ends. Wheels that were an
  uncapped tube with a bright disc inside — a hole with a hubcap floating in it.
  Bonnets two metres long.
* Asphalt aggregate at 13 cm per stone, so the carriageway looked like bubble
  wrap, and wheel tracks baked into a tiling texture, which put them *across*
  the carriageway repeating every three metres.
* Awnings built across a whole 20 m shopfront, roofing the footway.
* Chimney stacks five metres above the roof they sit on, from re-deriving the
  roof height instead of using the roof's own.
* Foliage cards with 24 cm leaves — a house plant hanging over the footway.
* Bollards eight to a crossing, which read as a car-park barrier.
* Signal lenses bright enough to bloom into a flare.
* Three viewpoints that were not places a person could stand: one inside a
  building, one in a running traffic lane a metre from a parked car.
* A sun at 38°, where 17 m buildings shadow the whole 18.6 m canyon and the
  street reads as sunless.

## Optimisation work done

* Per-material, per-cell batching: 787 static batches rather than one
  uncullable mesh per material or one draw per object.
* Instancing for every repeated prop; 408 instances in 54 groups.
* Frustum and distance culling per batch and per instance, with separate
  distances for drawing and for casting a shadow.
* Shared textures behind tinted materials: seven façade colours and a whole
  vehicle fleet's paint from one texture each.
* Full mip chains on every texture, without which a road at a grazing angle is
  a shimmer.
* Alpha masking instead of blending everywhere except glass, so nothing but
  glass needs sorting.
* The content pipeline, which takes the material stage from eight seconds to
  one.

## Asset work done

Every sign face, letterform, marking, pane of glass and mesh of the street
itself is generated from a seed by code in this repository. Sixty-nine models
and fourteen scanned PBR surfaces are not; the earlier count below is the
third pass's, kept as it was written: sixteen Khronos sample models behind
the shop glass, twenty Poly Haven scans standing in the street, and fourteen
Poly Haven texture sets on the surfaces the camera gets closest to. Every one
is declared in `assets/external/manifest.json` with its licence, its files and
their digests, fetched rather than committed, and checked by
`scripts/validate-assets.py`, which CTest runs. `assets/ATTRIBUTION.md` is
generated from the manifest.

## CNA issues found

`docs/cna-findings.md` has all seventeen with reproductions, severities and
workarounds. The one that was fixed rather than worked around is CNA-F12: the
content pipeline could not produce a mip chain, which made compiling a scene's
textures look worse than generating them. The fix is `feat/cnb-source-mipmaps`
on `openeggbert/cna`; `docs/patches/` carries the same commit as a patch, with a
README saying what it is and how to apply it.

`docs/cna-followup-after-framework-work.md` is the other half: the same
findings re-checked against the CNA tree as it stands, sorted into what blocks
the next phase of this project and what does not, with a repair order and the
work that follows each fix.

## The visual overhaul

A second pass over the whole project with the *rendered result* as the
acceptance criterion rather than the code. What changed, in the order the
defects were found by looking at pictures:

* **Lighting.** Two independent bugs made the street cold and flat: the sun's
  colour was taken from the sky's radiance beside the disc, which is scattered
  light and therefore blue, and the environment cube was Reinhard-and-sRGB
  encoded but read as linear. Sun colour is now atmospheric transmittance along
  the solar path, and the cube is a plain linear quantiser with its scale on
  `ImageBasedLightEXT::Intensity`.
* **The PBR catalogue.** `PbrEffect` multiplies the factor by the map, and this
  catalogue was writing the intended value into both, so every roughness in the
  city was its own square and every declared metal was a dielectric. The factors
  are now divided by what the generators actually wrote, and the divisors travel
  with the compiled content.
* **Vehicles.** Twelve variants over six classes, lofted from monotone-cubic
  profile curves with wheel arches cut into the body, separate rolling and
  steering wheels, interiors, lamps and plates, turning through the junction.
* **People.** One mesh on a nineteen-bone skeleton, skinned by distance to the
  limbs running *out* of each bone, animated by `AnimationPlayer` into
  `SkinnedPbrEffect` from clips built in code — and then three separate
  modelling errors found by standing one at two metres and looking at it:
  shoulder wings, a hairline drawn across the eyes, and hands the wrong way
  round.
* **Shops.** Every ground-floor unit is a room with fittings, stock, a counter
  and lit ceiling strips, dressed from imported glTF props, with its trade
  decided once per plot so the fascia and the fittings cannot disagree.
* **Surfaces.** Asphalt at four times the resolution with resolvable aggregate,
  a footway of sixty-four slabs rather than nine, and the crack, oil and patch
  fields cut back to what a road has rather than what noise makes easy.
* **Vegetation.** Trees with a recursive branch structure, foliage hung at
  three points along every twig and on the two levels above, and vertex normals
  bent toward the crown's outward direction so a canopy shades like a ball.
* **Night.** `--night`, and the street lights itself.
* **Performance.** +15% over the pre-overhaul baseline, after finding that the
  cost was draw calls rather than triangles and taking 1 997 of them to 1 356.

## The second visual pass

A third pass over the rendered result, with the first overhaul's own list of
what still gave the street away as the brief: car paint with nothing to
reflect, shop glass that reflected nothing, a far context of printed boxes.
Everything below is in `docs/visual-overhaul-2/`, with the frame each change
started from beside the frame it ended at.

* **Reflection probes.** The static street captured six ways into a cube from
  twenty-nine points along the carriageways at scene build, convolved by
  `EnvironmentProcessor` exactly as the sky is, and bound per draw through the
  same `ImageBasedLightEXT` the sky arrives by. A black car has a horizon on
  it; a shop window reflects the cars parked in front of it. Seven seconds at
  start-up, nothing per frame.
* **Glass.** Blended as reflection plus attenuated background -- premultiplied
  `AlphaBlend` set per draw inside the pipeline's transparent phase -- with a
  near-black reflection-layer tint and a low alpha, instead of a pale coloured
  filter that multiplied the Fresnel term by 0.24.
* **Shops.** Rooms painted to their trade in a front and a back shade, tube
  fittings a hand wide, posters on the walls, packaged stock instead of
  confetti, one unit in six shut behind a roller blind, and half the light.
* **Vehicles.** Tail clusters a hand tall that wrap the corner *toward* the
  car rather than 26 cm out behind it, brake lenses on the lamps they light
  rather than slabs on the bumper, chromed headlamp reflectors, a grille with
  slats and a badge, a roof aerial.
* **Facades.** Weathering as decals hung from the thing that causes it -- sill
  run-off, splash at the plinth, downpipe stains -- scaled by each plot's own
  weathering value; the render's crazing cut to one crack every few metres;
  planters and chairs on the balconies; satellite dishes.
* **The district beyond.** Kerbs, footways and carriageways continued to the
  far block; the blocks lining them with real window recesses, shopfronts,
  plinths and cornices; cross streets every third block; the same trees and
  parked cars along them.
* **Night.** A deep blue twilight sky with the afterglow kept to the sun's
  side, clouds that take the sky's colour once the sun is down, half a stop
  more exposure.
* **The street.** Bicycles on the stands, collars on the coats, caps on a few
  heads, six greens for six trees, a tree viewpoint aimed at a tree, and two
  photographic viewpoints.
* **Tests.** A tenth suite for the invariants behind all of the above, one of
  which found the tail-lamp bug on its first run.

## The third visual pass

A fourth pass over the rendered result, with a different acceptance criterion:
not "is this a convincing street" but "is this close to an environment-art
screenshot", and a different strategy: content before systems. Everything
below is in `docs/visual-overhaul-3/`, with before and after frames.

* **Scanned surfaces.** Fourteen Poly Haven PBR scans, all CC0, replace the
  generated asphalt, paving, kerb, three bricks, render, ashlar, concrete
  panels, roof tiles, plane bark and shop floor -- by name, in the content
  build, through `scripts/prepare-surfaces.py`, with the render neutralised so
  the catalogue can still tint it seven ways, the maps taken at face value,
  and the UV scale that keeps a brick the size of a brick. The generated
  surfaces stand wherever a scan is not fetched.
* **Scanned props.** Twenty Poly Haven models through CNA's own glTF importer:
  hydrants, cabinets, benches, planters with shrubs, cafe tables and A-boards
  outside the bakeries, crates and cartons by the doors, refuse sacks by the
  bins, manhole covers on the crown of the road, a car under a cover in a bay
  of the hero block. Each with the generated stand-in it replaces kept as the
  fallback.
* **Hero trees.** A scanned small tree cut in Blender to a near and a far
  street level of detail by `scripts/blender-tree-lod.py`, stood at the pits
  nearest the showcase viewpoints and scaled to a street tree's height; the
  generated trees everywhere else.
* **Light.** Sun 4.2, sky 0.85, exposure 0.9 in place of 3.0, 1.0 and 0.42: a
  sunlit wall at 85 % on the display instead of 35 %, a sky darker than the
  wall rather than brighter, and the shade three stops down rather than five.
  The probes' irradiance convolved from the capture at 1.6x for the bounces a
  single capture cannot hold, and the sunlit ground added to the sky's own
  environment.
* **A bug that predated all of it.** Every imported model had been drawing
  untextured since the first overhaul; the scanned props made it obvious and
  a one-condition fix in `MaterialLibrary::add` restored the textures on the
  Khronos shop props too.
* **The manifest, the fetch script and the licence gate** extended to
  multi-file assets, derived files and scanned surfaces, and the gate
  registered with CTest.

## The fourth visual pass

A fifth pass, with the narrowest brief so far: not the renderer but the
objects the viewer looks at first. Everything below is in
`docs/visual-overhaul-4/`, with before and after frames.

* **Hero cars.** Eight authored car models under CC-BY from Sketchfab authors
  publishing their own work, fetched from the Objaverse mirror with the
  provenance the file embeds, normalised in Blender by
  `scripts/blender-vehicles.py` and dealt into the parked bays of the hero
  corridor in place of the lofts, which stay in the simulation undrawn.
* **Hero people.** Eight people built from MakeHuman's CC0 base mesh and
  wardrobe with MPFB in Blender by `scripts/blender-people.py`, posed with the
  arms down, their authored weights folded onto this project's nineteen bones,
  and written in this project's own character format for `CharacterLibrary`
  to read -- the path around GLTF-208, which still stands. They drop into the
  crowd on the same skeleton and clips as the generated figures they replace.
* **Three tree species.** A broad island tree and a mature jacaranda beside
  the small tree, cut by the LOD script (which learned glTF input), planted
  over the whole main street within eighty metres of the junction, the
  generated trees beyond.
* **Architectural depth.** Chamfered sills, lintels, string courses, cornices,
  pilasters, balcony nosings and thresholds; projecting window surrounds on
  the flat elevations; brackets under the balconies; every shop door set back
  into a reveal with a threshold and a pull handle; meter cabinets, vents,
  conduit runs, scanned cameras over the shop doors and scanned condenser
  units beside the upper windows.
* **The hero cafe.** One shop built as a composed room -- counter and glass
  case, bread shelving, coffee station, pendants, a back door into a darker
  store, a dressed window, cafe tables -- with thirty-eight scanned props
  anchored by the interior generator and stood up by the scene.
* **Two CNA findings**, both worked around in the asset pipeline: CNA-F15, an
  imported single-sided glTF part draws inside out under the default cull
  (every earlier import was double-sided, which hid it), and GLTF-209, an
  instanced draw refuses a mesh with a second UV set or a colour attribute.
* **The manifest** grew a tools list for the Blender extension the people are
  built with, derived folders, and the credits CC-BY asks for, generated into
  `assets/ATTRIBUTION.md`.

## The fifth visual pass

A sixth pass, with the brief narrowed once more: not more systems and not
more assets, but the *discontinuities* between the good ones -- the loft
moving past the authored car, the walk that bent its knees the wrong way, the
scanned brick on a plane with holes in it, the composed cafe beside a box of
packets. Everything below is in `docs/visual-overhaul-5/`, with before and
after frames.

* **The cars driven.** `scripts/blender-vehicles.py` finds each authored
  car's four tyres -- by material name, by connected pieces of tyre size, or
  by the objects that stand on the ground -- and splits every wheel off
  into a node of its own centred on its axle; `CityScene` deals the eight
  models to all thirty moving vehicles by class, tells the simulation their
  lengths, and rolls the wheels from a new odometer at their own radius.
  Not one loft is drawn in a flagship frame.
* **Gaits.** The walk rebuilt from keyed gait curves with the knee bending
  the right way (the old one bent every knee forward and a row of eight
  frozen mid-stride showed it), hip and shoulder counter-rotation, a head
  that stays on its heading; three walks and three stances dealt per
  person, strides scaled to height, a lateral spread across the footway,
  turns over half a second, and one person in six walking with the one in
  front. `--lineup` freezes eight people at successive phases of the walk.
* **Facades.** Two-metre windows on the older blocks, and something behind
  every one: a net curtain, a half-drawn blind, a dark room; keystones and
  hoods on the masonry blocks, folding shutters on half the rendered ones
  (one in eight closed), quoins up half the corners. All from a stream of
  their own, so the rest of the street did not re-deal.
* **The cafe, and its neighbours.** An oak floor, ceiling beams, boarded
  panelling and a boarded counter front, timber shelving with bread and
  baskets, a shelf of tea things, a bench, bronze joinery and a hanging sign
  outside; and every other shop given the scans its trade would have --
  crates, cartons, baskets, plants, prints, a clock -- with its stock a stop
  darker and its tubes a third dimmer.
* **Mip chains for every imported image.** `ContentManager` looks for a
  `.cnb` before a loose file, so the content build compiles every image a
  model refers to under its own full name with a chain, in the colour space
  `scripts/model-textures.py` reads off the model: the workaround for
  GLTF-206 on this side of the framework, and what let the two closest cars
  carry 2k paint.
* **Supersampled stills.** `--supersample 2` renders a still at twice the
  size and box-filters it down in linear light: the flagship frames.
* **Tests.** A gait suite (a knee bends one way, an arm swings against its
  leg, the legs are half a cycle apart, the three walks differ), and cases
  for companions, strides, the lineup and the odometer.

## The sixth visual pass

A seventh pass, and the first one that began with somebody *walking* through
the street rather than looking at it. Six live-play defects, and the
draw-call consolidation that paid for fixing them. Everything below is in
`docs/visual-overhaul-6/`.

* **A car drove backwards.** Two faults: `rotation_euler` is an unused field
  on an object in quaternion mode, which Blender's glTF importer leaves
  every imported object in, so the turn the script wrote never happened and
  reported success; and which end of a car is its nose was a claim in a
  table that nothing checked. It is now measured from the body's own shape
  by a rule shared between the script that decides the pose and the CTest
  that verifies the exported files (`scripts/vehicle_pose.py`,
  `scripts/vehicle-orientation.py`).
* **A walk with the legs a third of a metre apart.** MakeHuman's A-posed rig
  diverges the legs all the way down -- hips 16 to 23 cm apart, ankles 29 to
  42 -- and nothing in a gait built in the sagittal plane brought them in.
  Each figure's thigh is now adducted by what that figure's own skeleton
  needs, and `gait_tests` evaluates the clips through both rigs and refuses
  feet more than 30 cm apart.
* **Four people in one body volume at a crossing.** Kerb queue slots chosen
  by position rather than by number, a staggered step-off on green, lanes by
  direction of travel, a follower-only pace rule and one ordered separation
  pass a frame. `pedestrian_tests` runs 120 people through four minutes of
  signal cycles and checks every pair at every sampled step.
* **A camera that walked through cars.** Vehicles are oriented boxes of the
  size of the model drawn for them; the street furniture and the tree trunks
  are solids; and a car that drives into the camera eases it out at walking
  pace instead of carrying it.
* **Thirty cars with nobody at the wheel.** Six rigid seated drivers, two
  draws each, inside thirty metres, in moving cars only.
* **The lofts among the authored cars.** Every parking bay within 126 m
  carries one of the eight models, dealt into four distance rings so a near
  car cannot promote the whole street's level of detail; the district beyond
  parks their far copies. Every tree pit carries one of the three scans.
* **Half the draw calls.** `scripts/vehicle-atlas.py` and
  `scripts/people-atlas.py` merge what can be merged -- a car from
  thirty-three primitives to eight, a person from six draws to three -- and
  the shadow pass is fitted to each cascade's slice of the camera frustum
  rather than to a disc around the camera. 81.3 ms to 59-63 on the machine's
  own Radeon 780M, with a third more triangles in the frame.
* **A face with relief in it.** A normal map derived from the skin albedo's
  own high frequencies, a cornea at 0.12 roughness, and a wrist turned so
  the palm faces the thigh.
* **`--walkthrough`**, which drives the walking camera along a scripted route
  through the same collision code a person's WASD goes through and reports
  what it met.

## The seventh visual pass

An eighth pass, in two halves that turned out to be one: make the city
hold up past the hero block, and stop guessing where the frame goes.
Everything below is in `docs/visual-overhaul-7/`.

* **Four live-play defects, reported from the keyboard.** The street was
  laid out for left-hand traffic under a comment saying right-hand -- the
  left of a car facing +Z is +X, which `Geometry::AlongFrame` had always
  said -- with eight left-hand-drive models on it; every driver sat in the
  passenger seat beside the wheel; the driver was a faceless two-draw
  prop; and five of the eight cars' wheel nodes carried an arch liner, a
  mudflap or a caliper that orbited the axle. The lanes, bays and signal
  kerbs keep right through one `TrafficSystem::rightOf`; the steering
  side is measured off each model's cabin by `vehicle-orientation.py`;
  the driver is the crowd's own imported figure playing a new `drive`
  clip; a wheel part rolls only if it is a surface of revolution about
  the axle.
* **Every imported model's box was a cube.** CNA's `ModelMesh` has a
  sphere and no box; a cube of the diagonal gave every car its length as
  its width and height. Measured from the vertices now (CNA-F18).
* **A GPU clock.** A `GpuTimer` per stage beside the CPU stopwatch, the
  post chain's own pass timers, per-cascade shadow work, and the shadow
  pass attributed by content family. The opaque pass is CPU-bound by 12
  ms of `PbrEffect::Apply`; the post chain is GPU-bound; the shadow pass
  was more than half scanned trees.
* **Half the shadow pass.** Trees and props cast from their far level of
  detail; the parked fleet casts from a shadow-only proxy; architecture,
  the district and people each have a shadow distance of their own.
  13.2 M shadow triangles to 6.5 M, GPU shadow 15.5 ms to 9.5.
* **The city behind the frontage.** Two rows of blocks behind the
  street-facing district on both arms of both streets, batched a strip
  at a time: +45 draws, +5.5 k triangles, and the field behind the
  street is gone from every elevated view.
* **Sound.** `SoundScape`, through CNA's own XNA audio: engines by speed
  with Doppler, horns, the walking camera's and the pedestrians'
  footsteps, voices at the crossings, wind and birds. NOX Sound's CC0
  packs, derived by `scripts/prepare-audio.py`.
* **A loading screen**, in place of twenty-five seconds of black window.

## The eighth visual pass

Finish the distant city, remove the application-side waste that could be
removed cleanly, turn the street into a reproducible benchmark, and write
down what now belongs in CNA. Everything below is in
`docs/visual-overhaul-8/`.

* **A caster is written only into the cascades its shadow can reach.**
  The receiver picks a cascade by view depth; the caster cull now asks
  whether anything the caster can shade lies at the depths a cascade is
  read for, by sweeping its sphere along the light to the ground
  (`SceneRenderer::casterShadowReachesSlice`, `shadow_cull_tests`). The
  flagship view's shadow pass went from 1 899 draws and 5.9 M triangles to
  874 and 2.2 M, the far cascade from 916 draws to 365, and the GPU shadow
  time from 10.4 to 6.6 ms, with fourteen of eighteen viewpoints pixel-
  identical and the rest differing only under the hydrant, which now
  casts from its generated twin instead of 6 200 scanned triangles.
* **The draw cost measured to the microsecond.** The opaque pass is timed
  in two halves: this side's material setters and `Apply` are 1.0 ms
  (0.8 us a call) and the framework's draws 34.8 ms (27 us a draw) on the
  flagship view; a tenth of the applies repeat the previous material, so a
  state cache here would save a tenth of a millisecond. Where the time goes
  is `BindDrawParams`, and it is recorded as CNA-F19.
* **The district as plots.** Each windowed block is a terrace of plots
  with its own render, frame and door colour, storey count and roof;
  windows with reveals, framed sashes, sills and heads; shutters, string
  courses, balconies and quoins on the rendered plots; doors in recesses;
  and windowed elevations on the ends that face a cross street. Batched a
  strip at a time. The painted rows carry a cornice and pilasters; the
  skyline scatter batches by sector.
* **A level of detail per parked car.** The four rings the seventh pass
  dealt the parked hero cars into are gone: a near group culled beyond
  45 m and a far group culled inside it (`InstanceGroup::minDistance`)
  give every copy the detail its own distance deserves, and the parked
  fleet reaches its far copies for the first time. The merge of a parked
  copy's parts by material is written and dormant -- CNA's loader gives
  every part its own texture objects (CNA-F20).
* **A benchmark mode.** `--benchmark <preset>`: six fixed workloads --
  baseline, shadow, traffic, crowd, city, post -- each a camera, a sun and
  a measurement window at a fixed clock step, written out as one line of
  JSON or a CSV row with the renderer, the load average, both clocks per
  stage, the post passes, the draws, the cascades and the memory.
  `scripts/benchmark.sh` runs them all into one file per revision;
  `benchmark_tests` holds the presets and the writers.
* **The post chain's two dials exposed.** `ssaoSamples` and
  `bloomIterations`, in the settings and on the command line, measured
  per variant in `performance.md`.

## Next

* **Brake lights on the authored cars.** A driven authored car shows no lit
  lens when it brakes; the loft did. A per-part emissive override for the
  parts whose material is named for a lamp is the next step.
* **Paint variety in the traffic.** Eight models over thirty moving cars
  means the same red Mini twice in one frame. A tint on the body-paint part
  where the texture is neutral enough to take one.
* **Faces at two metres.** MakeHuman's skins are painted, and at
  conversational distance a painted face is a painted face; at four metres,
  where a street is seen from, they hold. A scanned skin under CC0, or a
  higher-resolution skin texture, is the next step for the close viewpoints.
* **The props at 2k.** With every imported image compiled with a mip
  chain, the 1k cap on the Poly Haven scans is texture memory rather than
  shimmer; the hydrant and the trees would take 2k where the cameras get
  closest.
* **The other shops.** One is composed; the rest are dressed boxes. The hero
  cafe is the template.
* **Dynamic reflections.** The probes hold the static street. A moving car is
  reflected in a shop window only through the sky cube, and a person not at
  all. A probe re-captured every few frames near the camera, or a planar
  reflection for the nearest shopfront, is the next step for the one surface
  where it would still show.
* **Parallax-corrected probes.** A surface reads its probe's cube as though
  it stood at the probe. Box-projecting the lookup against the street's
  building lines would need the reflection direction remapped in the shader,
  which `PbrEffect` does not expose; a custom `ShaderEffect` for glass alone
  could do it.
* **An imported rig that draws.** `GLTF-208` is unresolved: the skeleton, the
  clip and the palette all round-trip correctly through the compiled model and
  the mesh renders nothing. Index element size is now ruled out — the models
  that do draw use 16-bit indices too. What is left, and the experiment worth
  running first, is in `docs/cna-followup-after-framework-work.md`.
* **One material per figure.** Skinned figures cannot be instanced, so a
  person costs three draws at any distance -- down from six, since
  `scripts/people-atlas.py` merged the parts that could be merged. Going to
  one means either putting the 2k skin through an atlas cell, which is the
  face, or alpha-testing the hair against the same atlas. It is the largest
  remaining draw-call item by a factor of four.
* **The district's last tier of cost.** The plots stand up to fifteen
  metres; inside that they show undressed windows behind one pane and
  flat shutters. A dressed-window variant for the plots nearest the end of
  the modelled street would take the mid tier to the footway; a single
  pane for the whole shopfront is the other tell.
* **A shadow caster mesh for the frontage.** With the slice test in, what
  the far cascade still draws is content whose shadow is on screen, and
  half of it is the frontage's window frames, ashlar and zinc: a caster
  copy of each plot without reveals, frames and mouldings would halve the
  far cascade again. Content work, on this side.
* **The bolted wheel parts.** The arch liners, calipers and mudflaps the
  wheel splitter swept up now sit still, at a draw each; folding them
  into the body node in `scripts/blender-vehicles.py` gives those back.
* **The parked-copy merge, when CNA shares textures.** `mergedByMaterial`
  is written and dormant on CNA-F20; the day the compiled-model loader
  hands parts sharing an image one `Texture2D`, the parked Punto is five
  draws instead of nineteen, with no change here.
* **Performance work moves into CNA.** The frame is 1 200 draws at 22 -
  27 us of driver time each (CNA-F19), 150 skinned draws that cannot be
  fewer (CNA-F14), 900 shadow draws that cannot be instanced (CNA-F6). The
  benchmark presets are the handover; `docs/visual-overhaul-8/benchmark/`
  is the baseline to diff against.
* **A skinned shadow caster in CNA.** CNA-F14. Every character currently casts
  with a rigid stand-in in its bind pose.
* **Sound, further.** The engines are four loops chosen by speed; a
  granular blend between them, tyre noise on the move, and a signal
  beeper at the crossings are the next things a listener notices missing.
* **A measured `low` preset.** Its decisions are reasoned rather than measured,
  because this environment has only a software rasteriser.

## Deferred until the CNA working tree is available

Everything above that needs a change *inside* CNA is parked, not abandoned. It
is written up in **`docs/cna-followup-after-framework-work.md`**: a repository
concurrency warning to read first, the seventeen findings re-checked against
the CNA tree as it stands, a repair order, and a startup checklist for a
session that begins with no memory of this one.

Three of the seventeen block the next flagship phase — GLTF-208, GLTF-207 and
CNA-F14, in that order, all in the same area of CNA. The rest are improvements
or already fixed upstream.

The sequence after those land, in dependency order:

1. Integrate the fixed CNA baseline: rebuild, re-run the tests, re-shoot the
   screenshot set, update `dependencies.lock`.
2. Remove the workarounds one at a time, each proven unnecessary before it goes
   — the `SkinsEXT`/`Tag` dual path, the sRGB-encode override, the sky flip,
   the shadow-bias constant.
3. Put an imported animated pedestrian in the crowd, which is what GLTF-208
   was blocking.
4. Imported authored vehicles, then vegetation assets.
5. Reflections, then a true many-light night mode.
6. A final realism pass with no framework dependency left in it.

Phases 3 onward each need an asset with a verified licence before they start.
The detail, including what must *not* happen, is in the handoff.

## Blockers

None in this repository. The three CNA-side blockers are named above; they
block the next phase, not the current state.

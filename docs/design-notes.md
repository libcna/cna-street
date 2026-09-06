# Design notes

The decisions behind the code, and what happened when they were made
differently. The README says what the project is; this says why it is shaped
the way it is.

## Proportion is the whole game

`street/include/CnaStreet/Scene/StreetMetrics.hpp` is the most important file
in the project. A street reads as fake long before anyone can say why, and the
reason is almost always proportion: a sidewalk you could park on, a car the size
of a bus, a doorway a giant would use. Nobody looks at a render and thinks "that
kerb is 40 cm"; they think "that looks like a game".

So every dimension is a named constant in one header with a note saying where it
comes from — German RASt 06 and EAE street-design practice for the highway, DIN
18065 for storey heights, StVO sign sizes, DIN 67523 for lighting geometry — and
every generator reads them. The lane is 3.30 m because that is what an urban
main road with buses and no trams is built to. The kerb is 14 cm. The lamp is
8.00 m with a 1.55 m outreach. A bollard is 90 cm, and a bollard 30 cm too tall
makes the whole street feel small in a way nobody can put their finger on.

Street furniture matters most of all, because it is the scale reference a viewer
actually uses. A building could be any size; a bench could not.

## The street is generated, and that is architectural. Its objects, and now fourteen of its surfaces, are not.

That heading has been rewritten twice. It read "everything is generated", then
"every surface is generated, sixteen models are not", and each change is worth
explaining rather than quietly making; the latest is under *Scanned surfaces
beside generated ones* below.

There is still no downloaded font or sound anywhere in this repository, and
every sign, glyph, marking, piece of glass, weathering decal and far facade
comes from code, as does every mesh of the street itself. Generation is not a
way of avoiding a licence question — it is a set of properties worth having:

* A generated surface can be produced at any resolution.
* It tiles by construction, rather than because someone made it tile.
* Its roughness map is derived from the same height field as its normal map, so
  the two agree, rather than being guessed at separately.
* It comes from a seed, so it is byte-identical on every machine that builds the
  repository — which is what makes a golden screenshot mean anything.
* The whole asset set is reproducible from source with one command and no
  network.

The cost is that each surface has to be *authored*. `TextureFactory.cpp` is a
description of what asphalt looks like — 8–16 mm aggregate in a bitumen binder
that greys as the binder wears off the stone, polished wheel paths, cut-and-fill
patches over a service trench with a sealed lip, branching fatigue cracks, oil —
not a call into a noise library. That is a lot of code for a road surface, and
it is the difference between a road and a grey plane.

**And there is a limit, which the shop windows found.** Thirty-nine ground-floor
units want something in them, and modelling a kettle, a pair of sunglasses and a
vase of flowers by hand in C++ is not a good use of anybody's time or a good
demonstration of anything. So the props behind the glass are imported glTF
models, compiled through CNA's own importer and content compiler — which is
itself the point, because it exercises the framework's pipeline end to end
rather than a second one written beside it.

The rule that replaced "generate everything" is narrower and more defensible:
*the street is generated, because a street is a set of relationships and a
relationship is code; the objects standing in it may be authored, because an
object is a shape and a shape is data.* And an authored object arrives with a
licence, which is why `assets/external/manifest.json` records sixteen of them
one at a time and `scripts/validate-assets.py` refuses to let an undeclared file
reach the content build.

## The scale a texture is drawn at is a property of the material

The single most instructive bug in the project: the asphalt drew its aggregate
as 46 cells across the tile, and the road lays that tile at six metres, so every
stone was 13 cm across. From the footway the carriageway looked like bubble
wrap.

A 6 m tile in a 512 px map is 1.2 cm per texel. The largest aggregate that can
be *resolved at all* is about 4 cm, and drawing anything finer is drawing noise.
The fix was not a better noise function; it was doing the arithmetic.

The same arithmetic says where a feature cannot live in a material at all. Wheel
tracks are a property of a *lane* — of which way the traffic runs — and a tiling
texture has no idea which way that is. The version that drew them in the
material put them across the carriageway, repeating every three metres, which is
a road surface no vehicle has ever made. They are decals along each lane now.

## Winding, and why it deserves its own function

CNA follows XNA: with the default `RasterizerState::CullCounterClockwise`, a
face is visible when its vertices are clockwise as seen from the front. That
convention lives in exactly one place —
`MeshBuilder::addTriangleIndices` — and it was determined empirically rather
than assumed, with a probe program, because getting it backwards is not a crash.

Getting it wrong produces geometry that is *visible but lit from behind*: a roof
slope that stays grey in full sun, a lane marking that renders black on black
asphalt. Or geometry that is invisible from the only side anyone looks at it
from: a shop with no back wall, a car whose near-side panels are culled so you
can see the inside of its far side. All four of those happened here.

So `addQuadFacing(a, b, c, d, hint)` exists: where the intended facing is known
and the corner order comes from something else — a direction of travel, a side
of the street, five faces of a room in façade coordinates — state the facing and
let one function sort the order out. The room in a shopfront had its back wall
and ceiling culled precisely because a human derived five corner orders by hand
and got three of them right.

## Batching: the compromise nobody likes but everyone needs

Two things have to be true at once. Cheap to draw means as few draw calls as
possible, which argues for one enormous mesh per material. Cheap to cull means
the renderer can discard what is behind the camera, which argues for many small
meshes. A road batched as one 260 m mesh is one draw call that is always visible
and always fully rasterised.

`GeometryCollector` merges per material *within a 34 m grid cell* — a little
larger than a building plot, which is the natural granularity of a street. 42
buildings become a few hundred batches rather than tens of thousands of draws or
a handful of uncullable monsters.

Its one non-obvious property is documented in the header and is not a detail:
`builder()` returns a reference that stays valid. The generators hold several at
once — a wall, a frame and a pane of glass are interleaved while one window is
built — and an implementation that stored the builders directly in a
`std::vector` invalidated the first reference the moment the third material was
asked for. It did, and the result was a use-after-free that looked like a crash
somewhere else entirely.

## Neutral textures, tinted materials

The plaster, painted-metal, fabric and car-paint generators produce a *white*
surface: all the pattern, none of the colour. The colour arrives as the
material's base colour.

Seven façade colours cost one texture rather than seven. A fleet of ten
differently painted cars costs one texture set, not ten. Twenty painted-metal
variants — the signal housings, the lamp columns, the railings, the bins — cost
one. That is the difference between 40 MB of texture memory and 400 MB, and it
costs nothing visually, because the pattern really is the same on a green pole
and a grey one.

The exception proves the rule: the shop fascias each have a different name baked
into the board, so they genuinely cannot share, and they are the one place the
catalogue registers a material with textures of its own.

## Linear light, and the one place it is encoded

Every colour constant in the generators is quoted the way a paint chart quotes
it — as an sRGB byte triple — and converted to linear once, at the point it is
written. Textures are uploaded as sRGB and sampled to linear. Lighting,
tone mapping and post-processing are all linear. The encode back to sRGB happens
exactly once.

Where "exactly once" is depends on where the frame is going. `PbrEffect` encodes
its output to sRGB by default, which is right when it draws to the back buffer
and wrong when it draws to the HDR scene target, because the pipeline's tone
mapper encodes as well. With both, a 10:1 albedo ratio rendered as 1.6:1, and
the whole street looked washed out in a way that was very hard to attribute. The
renderer now tells the effect which case it is in, every frame.

## The sun is a composition decision

At 38° elevation, 17 m buildings shadow the entire 18.6 m canyon. The street is
uniformly in shade, there is no contrast anywhere, and it reads as sunless — and
the natural conclusion, when this happened, was that the shadow pass was broken.

It was not. Dumping the cascade atlas, logging the splits and differencing two
renders showed 21 % of pixels changing with shadows on and off. The shadows were
working perfectly. The scene simply had nothing lit to compare them against.

The default is 48° now, where the shadow reaches 15.3 m of the 18.6 m street and
the frame has both light and shade in it. This is the sort of thing that looks
like a rendering bug, is diagnosed like a rendering bug, and is a composition
decision.

## People: skinned after all, and what that cost

This section used to argue that posing a figure at eight phases of a stride was
a deliberate trade — smooth enough that nobody counts them, and cheaper than the
skinned pipeline's per-bone state. The argument was sound and the conclusion was
wrong, for a reason no amount of reasoning about *motion* was going to reach: the
figures did not read as people, and the fault was never the animation. It was
the silhouette. No neck, a cylinder for a torso, a sphere for a head, no hands
worth the name.

So they are skinned now: one mesh per person on a nineteen-bone skeleton,
weighted by distance, animated by `AnimationPlayer` into `SkinnedPbrEffect` from
walk and idle clips built in code. Three things are worth writing down.

**The bind is off by one in the direction that looks right.** A bone catches the
flesh of the limbs running *out* of it to its children, not the limb running
into it from its parent. Measuring the latter is defensible on paper and puts
the upper arm's flesh on the forearm bone, so every limb bends about the wrong
joint — and it looks like a rigging problem rather than an indexing one.

**A keyframe replaces a bone's whole local transform.** A track that leaves the
translation at zero collapses its bone onto its parent. The first figures had no
legs and one arm growing out of the neck, which is not a subtle failure and was
still not obviously a *keyframe* failure.

**The modelling errors outlive the rig.** With the skeleton correct and the
clips playing, the figures still read as mannequins, and the three causes were
all geometry: an arm whose first section sat above the shoulder line beside the
neck, which is a wing; a hairline ring drawn low enough to cross the eyes, which
is a welding mask; and hand sections eight centimetres across and ten deep,
which is a boxing glove. None of those is visible in a diff and all three are
obvious the moment a figure is stood at two metres and looked at.

That last point is the general one. The animation was the interesting problem
and the silhouette was the one that mattered.

**What it costs.** A skinned figure cannot be instanced — each carries its own
bone palette — so it is one draw per material at every distance. The material
set therefore collapses past twenty metres: shoes and bag onto the trousers,
eyes onto the hair, hair onto the trousers, three draws instead of six for a
figure that is fifty pixels tall. And people have a cull distance of their own,
because a person at 210 m is under four pixels and costs the same three draws as
a person at three metres.

## A factor and a map are not the same number

`PbrEffect` computes `roughness = map.g * roughnessFactor`. That is glTF's rule,
it is the right rule, and this catalogue broke it on every one of sixty-odd
surfaces in the most plausible way there is: the generator is handed the
surface's roughness, writes it into the map with its own variation around it,
and then the material declares *the same number* as the factor. The product is
the square.

Nothing about that looks wrong in a diff. `pbr(0.62f, 0.0f)` beside
`paintedMetal(size, seed, rgb, 0.62f)` reads as two statements of one intent,
and it is two multiplications. What gave it away was printing both columns:

```
painted        R: 0.380 x 0.422 = 0.160
sign-street    R: 0.300 x 0.372 = 0.112
wheel-track    R: 0.580 x 0.670 = 0.389
car-body       R: 0.160 x 0.086 = 0.014
```

Sixty rows where the factor and the map mean are the same number twice. Painted
metal asked for 0.38 and got a gloss lacquer, so every lamp post, bollard, bin,
sign back and window frame in the city was lacquered. A wheel track asked for
0.58 and got 0.39, which is why the carriageway looked wet from a low camera.

Metalness failed the other way and silently. A material that declares itself
metal over a map whose blue channel is zero is a dielectric however emphatic the
declaration, so every galvanised post and alloy wheel in the scene was plastic
and a metallic basecoat had its flake multiplied out by a factor of zero.

The fix divides each declared factor by what the generator actually wrote, so
the product averages the declaration and keeps the map's spatial detail in
proportion. The divisors are measured at bake time and travel with the compiled
content, because the content path never runs a generator and a fix that only
works when the textures are generated is not a fix.

The transferable part is not the arithmetic. It is that a rendering bug spread
evenly over every surface in a scene does not look like a bug — it looks like a
*style*, and the only way to find it was to stop looking at the picture and
print the numbers.

## Two placement loops that must agree, in one place

Lamps and street trees both go in the furniture zone beside the kerb. Two loops
each picking their own spacing will eventually plant a tree inside a lamp
column, and that is exactly the kind of mistake that survives every unit test
and then dominates a screenshot.

So the rhythm lives in one struct: lamps on a 24 m beat, trees on the 12 m
half-beat offset by six. The closest a tree ever gets to a column is 6 m — about
right for a real street, where the lighting engineer and the tree officer are
also obliged to talk to each other.

## Testing what has no picture

The unit tests are chosen for what a screenshot cannot see.

Both arms of the junction green at once, or a green man across a street whose
traffic is running, are *safety* properties: invisible in any still, and
catastrophic in motion. They are asserted at every sample of three whole cycles.

Two plots occupying the same ground is the defect that put a blank flank wall on
all four corners of the junction — visible in a screenshot, certainly, but only
if you happen to look at that corner, and trivially assertable.

A quad wound the wrong way is visible but lit from behind, which is exactly the
kind of wrong that survives review.

The framework is forty lines. What is under test is arithmetic and a state
machine; a failure needs to say which check failed, on which line, with the two
numbers that disagreed, and then stop. A framework dependency would say the same
thing and bring a build dependency with it.

## Changing CNA rather than working around it

The rule followed here: work around it in the application when the gap is this
project's problem, and fix it upstream when the gap is the framework's.

Eleven of the twelve findings in `docs/cna-findings.md` are worked around —
they are papercuts, documentation gaps, or things this project wants that the
framework has no obligation to provide.

The twelfth is different. `cna_tool_source_to_cnb` compiled every texture with
exactly one mip level, and the container had always been able to carry a chain.
The practical consequence is that compiling a scene's textures makes it look
*worse* than generating them at run time, which is not a trade anyone should
have to make, and no application-side workaround recovers it without throwing
away most of what the pipeline is for. That belongs in the codec, so it went
there — with the colour space as an argument rather than an assumption, because
averaging sRGB-encoded texels as if they were light darkens every level, and a
data map must be averaged exactly as it is stored.

## A reflection is a picture of the street, so take one

The first overhaul's honest conclusion was that a dark car was a dark shape
and a shop window reflected nothing, and that both were the same problem: the
only environment the renderer had was the sky. A car door in a street canyon
reflects the facade opposite, the kerb, the cars parked beyond it and a strip
of sky between the eaves. None of that is in a sky cube.

Screen-space reflection was tried and dropped, and the reasons stand: it
cannot reach an alpha-blended pane and it has nothing to show on a rough
road. What worked is older and simpler. At scene build the static street is
rendered six ways from twenty-nine points along the carriageways -- over each
parking lane, at the height of a car door -- into a small cube, and every
draw near a probe reads its image-based lighting from that cube instead of
the sky's, through the same `ImageBasedLightEXT` the sky arrives by. The
effect never knows. The cubes are stored at the sky cube's own scale so one
`Intensity` serves whichever fills each slot, and convolved by CNA's
`EnvironmentProcessor` exactly as the sky is, because a second convolution
would be a second place for the two to disagree.

Three things were not obvious.

**A cube map is addressed from inside, a camera image from outside.** The
face rendered by a camera looking along +X with +Y up has +Z on its right;
the cube's +X face has +Z on its left. Every face is mirrored in one axis,
consistently, and a face copied without the mirror is a reflection of the
wrong side of the street -- with nothing anywhere to say so. The test suite
pins the convention against `SkySystem::cubeDirection`.

**Shadows in a probe.** The cascades are fitted to a view frustum, and a
probe needs shadows in every direction. Fitting them from a camera looking
straight down at the probe from 55 m puts every surface in the block in the
same one or two cascades whichever face is drawn, because the receiver
selects a cascade by depth along the fitting camera. One shadow pass per
probe, not six.

**Eight bits.** The capture target is `Color`, and a sunlit wall is 1.6.
Rendered at half brightness with the effect's sRGB encode on and decoded on
the CPU, a dark road keeps a dozen shades and the wall survives; the sun disc
clips, and a clipped sun disc in a reflection is a glint.

What it costs is seven seconds at start-up and nothing per frame: a probe is
a texture bound for a run of draws. What it buys is the largest single
change in the second pass -- a black car with a horizon on it.

## Glass is a reflection over what it covers

The probes made the second half of the same problem visible. A pane of glass
was blended as a coloured filter: `out = lit * alpha + behind * (1 - alpha)`,
under `RenderPipeline`'s `NonPremultiplied` state. Its Fresnel reflection was
multiplied by a 0.24 alpha and vanished, and a pale base colour under that
blend painted a milky wash over every interior on the street.

Physically the pane is two layers: a reflection, at full Fresnel strength,
and behind it whatever the glass transmits. That is `out = lit + behind *
(1 - alpha)`, which is XNA's premultiplied `AlphaBlend` applied to an effect
that does not premultiply -- and the pipeline's blend state turns out to be
a default, not a contract, so a material can ask for it. Under that blend the
base colour is the tint of the *reflection layer*, near black for clear
glass, and the alpha is how much the pane blocks of what stands behind it:
0.14 for a shop window, 0.55 for tinted automotive glazing. The shop
interiors then had to come down by half, because a lit room seen through a
clean pane from a sunlit pavement is not brighter than the pavement.

## Weathering is causal, or it is noise

The first overhaul put rain streaks in the render texture, evenly, and they
read as noise, because a tiling material cannot know where a sill is. Real
staining hangs *from* something: it fans down from under a sill where the
drip has failed, it rises from the pavement where the rain bounces, it
follows a leaking downpipe joint down the wall. So the streaks are decals now,
placed by the thing that causes them, a few millimetres over the wall, and
each one more likely and longer on a plot with a higher weathering value.
One building is grubby and its neighbour freshly painted, which is what a
street is; the tiling texture keeps only the fine stipple and one crack every
few metres.

## What "no fake complexity" meant in practice

The project deliberately does not have: a scene graph (a street does not move),
an entity-component system (there are four kinds of moving thing), a material
editor (there are 70 materials and they are in one enum), an abstraction over
CNA (there is one renderer and wrapping it would only add a place for the two to
disagree), or a plugin system.

It does have one enum with 70 entries, one dimension header, one mesh builder,
and generators that read like descriptions of the thing they build. Where there
is a class, it is because something owns state that outlives a function.

## Scanned surfaces beside generated ones

The section above that says every surface is generated is no longer true, and
the reason it stopped being true is worth more than the sentence it replaces.

The second pass ended with a street whose proportions, layout and light were
right and whose *surfaces* still said "procedural": a render that was a noise
field with speckles, a footway that was a grid with a crack texture, an
asphalt that was dark grey with weather on it. Each generator had been pushed
about as far as a description of a material can go in code, and the gap that
remained was the gap between a description and a photograph. So fourteen of
the surfaces the camera gets closest to are now photogrammetry scans from Poly
Haven, all CC0: the asphalt, the paving slabs, the kerb, three bricks, the
render, the ashlar, the concrete panels, the roof tiles, the plane-tree bark
and the shop floor. Everything else -- signs, glass, markings, packaging,
weathering, the interior atlas, the far facades -- is still generated, because
those are things a scan cannot be *of*.

Three decisions made the scans fit a catalogue built for generators.

**A scan replaces a generated surface by name, in the content build, and
nowhere else.** `scripts/prepare-surfaces.py` writes the three maps under the
catalogue's own name over the ones the bake wrote, and the runtime loads
whatever the content root holds. A tree that has not fetched the scans gets
the generated surfaces; a tree without a content build gets them too. Nothing
in the material catalogue names a scan.

**The render is neutralised, not replaced per colour.** The catalogue tints
one white plaster seven ways, and a scanned plaster arrives cream. Dividing
every texel by the image's mean colour in linear light keeps the pattern as a
set of ratios -- the trowel marks, the hairline cracks, the dirt under the
sills -- and removes the cast, so seven facade colours still cost one texture.

**A scan's maps mean what they say.** The catalogue divides each declared
roughness by what its generator wrote, because the generator wrote the
intended value into the map and the factor both. A scan's roughness map is a
measurement, so for a scanned surface the factors are 1 and the map is the
answer. `authored.txt` in the content root says which surfaces those are,
carries the UV scale that maps the geometry's tile onto the scan's physical
size -- a 3 m asphalt scan over a 5 m road tile is a scale of 1.667, and a
brick the size of a brick is the whole point -- and says whether the tint is
kept.

## Which way is up in a normal map

The scans arrived in the OpenGL convention, green up the image, which is what
glTF specifies and what the imported models use. Laid on this project's own
meshes they rendered inside out, and it took a synthetic map of hemispherical
bumps under a low sun to see it: the bumps came out as bowls. The mesh
builder's tangent frame sets its handedness so that the bitangent runs along
*increasing* v -- down the image -- which is the opposite of the glTF
convention's, and the catalogue's own generator has always written green along
increasing v to match. Both are self-consistent and neither is wrong; they are
the DirectX convention, and a scan has to be converted to it. The imported
models keep their own tangent frames from their own files and are unaffected.
`prepare-surfaces.py` inverts green unless the manifest says the scan is
already DirectX, and the reason is written where the flag is.

## An imported model's textures were being thrown away

The shop-window props imported in the first overhaul had been rendering
without their textures since the day they were added, and nobody noticed
because a vase of flowers is white and a cardboard box is brown with or
without one. `MaterialLibrary::add` uploaded whatever maps it was handed and
stored the results in the material -- including the null an empty map uploads
to -- over the pointers the importer had already put there. The scanned street
props made it obvious: a chalkboard is not white. The fix is one condition per
map; the lesson is that a fallback that looks plausible is more dangerous than
one that looks broken.

## One bounce is not enough

With the surfaces right the light was wrong in a way the first two passes had
tuned around rather than fixed: the frame was a stop and a half underexposed,
the sky was brighter than a sunlit wall, and the shade was black. The sun was
3.0 against a sky whose horizon radiance was 1.3, which is a sky three to four
times too bright for its sun, and the exposure of 0.42 had been chosen to keep
that sky from clipping. The defaults are now sun 4.2, sky 0.85 and exposure
0.9: a sunlit render at about 85 % on the display, a zenith at about 55 %, and
the shade a little under three stops down, which is what a photograph of a
street on a clear day shows.

The shade needed one more thing. The reflection probes give every surface the
irradiance of the street it stands in, which is right, and what they capture
is one bounce, which is not enough: the wall opposite is in the capture, the
light that wall throws onto the wall beside it is not, and in a canyon of
light render most of the ambient light has bounced more than once. Turning the
probes' irradiance off and using the sky's put the shade where it belonged and
lost the canyon; multiplying the capture by 1.6 before its irradiance is
convolved -- `probeBounceGain`, about the geometric series for walls of 0.45
albedo seeing half a hemisphere of each other -- keeps the canyon and lifts
the shade the same amount. The specular keeps the plain capture, because a
reflection is a picture and a picture is not brightened by the light behind
the camera. The sky's own environment also gained the sunlit ground under it,
which lights the underside of a car and the soffit of a shopfront and was
missing.

## The hero corridor

Not every plot got the same attention, on purpose. The showcase viewpoints
stand on the west footway of the main street between the junction and about
sixty metres north of it, and that stretch is where the scanned trees stand,
where the covered car is parked, where the cafe tables are out and the
deliveries are stacked by the doors. The rest of the district gets the same
scanned surfaces and the same scanned furniture -- hydrants, cabinets,
benches, planters, manhole covers are instanced everywhere -- but its trees
are the generated ones and its bays hold generated cars. That is how
environment art is done: the eye judges a place by what it can inspect, and
the budget goes where the eye goes.

## The weakest prominent object sets the level of the frame

The fifth pass was run on one heuristic: at every flagship viewpoint, find
the first object the eye identifies as fake, fix it, render again. That
sounds obvious and is not what the earlier passes did -- they added
systems, then assets, and each addition made the thing beside it look
worse. An authored Astra parked beside a lofted car in the travel lane is
not "one good car and one adequate one"; it is a frame that demonstrates
what a loft looks like. The same goes for a scanned tree beside a
generated one, a composed cafe beside a box of packets, and a scanned
brick on a plane with sixty identical holes in it. The rule that came out
of it: never let two adjacent things be two quality generations, and where
that cannot be afforded, take the better one out rather than leave the
worse one in.

## Driving a car whose author did not separate the wheels

An authored car model from Sketchfab is one mesh, or forty, and the
wheels are wherever the author left them: a material named `Tire_MAT`, an
object named `Wheel 2.003`, a node named `gum025` whose children are the
tyres, or nothing at all -- the Astra is a soup of three hundred thousand
loose triangles with doubled vertices along every seam. To drive it the
scene needs four wheels it can roll and steer, so
`scripts/blender-vehicles.py` finds them rather than trusting the file. A
tyre is the one part of a car that is a cylinder of a known size standing
on the ground, and three things can name it: a material with a tyre word
in it, a connected piece of the merged mesh whose bounds are a tyre's, or
the objects that touch the ground once the backdrop is dropped. Whichever
finds four, the axle is then the centre of each tyre's own bounding box --
not the ground, which the front pair may hang a hand above, and not the
contact patch, which sits to one side on a model a few centimetres off
level; the first version took both and every wheel hopped. Everything
whose bounds lie inside the tyre's cylinder turns with it (rim, cap,
disc), a caliper does not, and each wheel is written as a node centred on
its axle so the scene reads the axle off the node transform. The lofts'
`wheelAngle` could not drive them: it is scaled by the loft's radius and
wraps at one turn of that wheel, so a wheel of another radius jumped once
a revolution. An odometer that never wraps did.

## A walk is several curves, not one sine

The first walk was a sine per joint: a thigh swinging, a knee bending
whenever the leg was behind, arms opposite. A row of eight figures frozen
at successive eighths of the cycle -- the pedestrian lineup now does this
-- showed two things at once: every knee bent forward, because the
skeleton's positive pitch swings a bone's far end backward and the sign
had been guessed, and the gait had one shape where a walk has several.
The rebuilt walk is keyed: the thigh forward to heel strike, back through
stance to toe-off at six tenths of the cycle, forward through swing; the
knee nearly straight at heel strike, a little flex under load, straight
at mid-stance, sixty degrees at mid-swing; the ankle toes-up on landing
and toes-down on push-off; the arm against the leg on its own side and
trailing it a little; the pelvis bobbing twice, swaying onto the stance
leg, carrying the swinging hip forward; the trunk counter-rotating and
leaning in; the head turning back against the trunk. Three amplitudes of
it make three gaits, three stances make three ways of waiting, and each
person is dealt one of each with a stride scaled to their height -- the
clip's clock is distance over stride, so the feet stay on the ground in
all of them. The signs are pinned by `gait_tests`, which refuses a knee
that bends forward.

## Which side of the road, and which side of the car

Two claims had stood in comments since the first pass: that the street
kept right, and that a driver sat at -X. Both were wrong, and both were
wrong in the same way -- a statement about geometry that nothing measured.
The frame's own convention, `Geometry::AlongFrame`, had always said the
left of a heading (ux,uz) is (uz,-ux), so the left of a car facing +Z is
+X; the northbound lane at +X was on its own left, and a seat at -X was the
passenger's. The fix is the same shape as the sixth pass's fix for a car
that drove in reverse: one rule in one place (`TrafficSystem::rightOf`),
every consumer written through it, and a test that checks the rule rather
than four hand-written signs. The steering side is measured off each
model's own cabin -- the triangle area either side of the dashboard slab
-- because a car model is a file and a file can be right-hand drive.

## A shadow does not need the leaves

The shadow pass was more than half scanned trees. `shadowReport()` said so;
nothing else could have, because the registered-scene and visible-set
tables only ever counted what the eye sees, and a caster is written into
every cascade it can reach. A canopy silhouette on a cascade whose ground
sample is metres wide does not need the geometry a canopy needs at three
metres from the eye, so an instanced caster now draws from its far level
of detail at any distance, and the parked fleet from a shadow-only proxy
whose materials -- a shadow reads only positions -- need not match. The
general rule: **the eye and the shadow map are two viewers with two
budgets**, and content was being cut for one and charged to both.

## Sound is placed, or it is a soundtrack

Every source that has a place in the world is a `SoundEffectInstance` with
`Apply3D` against a listener at the camera: the mixer does the attenuation,
the pan on the listener's own right axis and the Doppler from the emitter's
velocity, and this side only has to say *where* things are and *what state*
they are in. An engine's sample is picked by the car's speed and bent
inside the band by it; a footstep is played when a person's walk phase
crosses half a stride, the same clock their legs run on, so the sound is
on the foot; the birds are in whichever tree is nearest, because that is
where birds are. The one unplaced sound is the wind, which is nowhere in
particular and so does not pan or fall off.

## A wheel turns about its own axle, which is not always X

The wheel splitter centres each wheel on its axle and the scene rolls it
about X. Concentric is not aligned: five of the eight authored cars export
their wheels with toe or camber baked into the mesh, up to eighteen
degrees on the Astra, and a wheel rolled about an axis eighteen degrees
from its own wobbles by thirty-six every turn. The axle is a property of
the geometry -- a tyre is a ring, a ring has one direction of least spread
-- so it is measured from the vertices at load and the wheel is rotated
onto X before anything else is done to it. The lesson is the sixth pass's
again: a frame convention nothing checks is a claim, and this one was
checked by somebody watching a yellow car's front wheel.

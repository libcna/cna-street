# The sixth pass: behaviour, coherence and cost

The five passes before this one made the street *photograph* well. This one
started when somebody walked through it instead, and found six things a
still cannot show:

* a car driving down Lindenstrasse in reverse;
* women walking with their legs a third of a metre apart;
* four people at a crossing standing in one another;
* a camera that walked through parked cars;
* thirty moving cars with nobody at the wheel;
* and, between the eight authored cars, lofted ones that gave away where
  the modelling stopped.

Every one of those is fixed, and fixing them turned out to be the same work
as the pass's other half -- making the whole frame hold together from the
foreground to the vanishing point, and paying less for it. The street now
runs at 60 ms a frame on the machine's own GPU where it ran at 81, with
more authored content in it than before.

Baselines, recorded before anything changed:

| Repository | Baseline | Note |
| --- | --- | --- |
| `cna-street` | `6a40427` on `develop` | The commit this pass starts from |
| `cna` (`../cnanext`) | `next` `3faa193` | Read-only throughout; another agent moved it during this session |
| `sharp-runtime` (`../sharp-runtimenext`) | `next` `bfc826e` | Read-only |
| `easy-gl` | `develop` `deda7a4` | Read-only |
| `meta-gl` | `develop` `20c8b2d` | Read-only |

**No framework repository was modified.** Every change in this pass is in
`cna-street`. Nothing new was found wrong in CNA; the two behaviours this
pass worked around, CNA-F6 and GLTF-206, were already recorded.

---

## 1. The car that drove backwards

Two faults, one on top of the other, and the second is the interesting one.

**The turn never happened.** `scripts/blender-vehicles.py` normalises a
downloaded car by writing `o.rotation_euler.z += math.pi` and applying the
transform. Blender's glTF importer leaves every imported object in
QUATERNION rotation mode, where `rotation_euler` is an unused field: the
write turns nothing, `transform_apply` reports `FINISHED` having applied an
identity, and the euler comes back zeroed -- so the code reads exactly as
though it had worked. The `flip=True` the fifth pass added for the Mini was
set, was believed, and did nothing. Turning through `matrix_world` works
whatever the rotation mode, and `turn()` is now the only way that script
rotates anything.

**Which end is the nose was a claim nobody checked.** It is now measured
from the body's own shape, by a rule shared between the script that decides
the exported pose and the check that tests it (`scripts/vehicle_pose.py`):
a car's cabin sits behind its middle, and the deck ahead of the windscreen
is longer than the deck behind the backlight. Both hold for a saloon, a
hatchback, a coupe and a van, and on these eight they agree on every one.

The profile they read is weighted by *surface area*, and that is the whole
difference between a rule that works and one that does not. The first
version took the tallest vertex per slab of the length, and the Mini's roof
aerial -- sixty centimetres of rod, a few hundred triangles, about a
fiftieth of a square metre -- became "the tallest part of the car", at the
back, so the rule turned the Mini round. An aerial, a mirror stalk and a
wiper all have height and no area; a roof has both.

`scripts/vehicle-orientation.py` is the test: a pure-Python glTF reader, no
Blender, registered with CTest as `vehicle_orientation`, which fails when
any derived car faces -Z or has its front wheels behind its rear ones --
the same error one step further in, since the front pair is what the scene
steers. `--sheet` draws all eight in side view with the nose expected to
the right, which is `vehicle-orientation.png` here.

The `--walkthrough` harness now samples every moving vehicle every step for
the angle between its drawn forward and the direction it is travelling.
Over 168 474 samples the worst is **2.88 degrees**, which is the tangent
sampling on the junction Bézier. A car facing backwards reads 180.

## 2. The wide-legged walk

MakeHuman's base mesh stands in an A-pose, and its rig does not merely
start the legs at the hips -- it *diverges* them all the way down. Read off
the eight derived people, the hip joints are 16 to 23 cm apart and the
**ankle** joints 29 to 42, the femur angling out where a real one angles
in. Nothing in the gait touched the frontal plane, so every imported figure
walked with its feet a third of a metre either side of its centre line.

`AdductionFor` reads each rig's own hip width, ankle width and leg length
and brings the thigh in at the hip by however much that figure needs --
five to ten degrees on the imported people, two or three on the generated
ones -- rolling the sole level again at the ankle so the foot lands flat.
It never pushes a leg outward, and the target is a fraction of that
figure's own hip width scaled by a per-person `stance`, so the tall man
still walks wider than the small woman and neither of them walks astride a
kerbstone.

`gait_tests` evaluates the clips through both rigs the project animates --
the generated one and a copy of the widest imported one -- samples the
whole cycle, and refuses feet more than 30 cm apart or less than 3. It
measures 13 to 15 cm now. `lineup/stride-before.png` and
`lineup/stride-after.png` are the same figure at heel strike, where a
stance is widest.

`--lineup` grew a third row for this: eight standing, eight frozen at heel
strike, eight through the walk cycle, each with a square front view.

## 3. Four people in one body volume

A crossing edge starts at the kerb, and everyone waiting for a green man
stood at distance zero on it. Four who arrived together stood inside one
another.

They now take a place in a loose cluster: five abreast at 0.72 m, three
rows deep, filled from the middle out and sized by the footway actually
behind the walking line -- 1.9 m on the main street, 1.3 on the side
street, which is what caps the rows at three. The place is chosen by
*position*, not by slot number, because the two crossings at a junction
corner start from the same node at right angles, and slot three of one is
half a metre from slot three of the other. On green the front row steps off
first, half a second a row, and each of them sets off from where they stood
rather than teleporting onto the crossing's centre line.

Walking, three rules that cannot oscillate: each direction of travel keeps
to its own side of the footway, so a pair meeting head on is already 0.8 m
apart; a follower takes the pace of the person they are following, and only
a follower, so no ring of people can wait on each other; and one ordered
separation pass steps anyone standing in somebody else aside across their
edge and back along it.

`pedestrian_tests` runs a hundred and twenty people through four minutes of
signal cycles and checks every pair at every sampled step. The busiest kerb
holds six or seven; **the closest two standing people come is 0.62 m**; and
52 of 3 427 200 sampled pairs pass inside 0.20 m -- momentary, at a node,
while the sidestep works. Before this it was a permanent state.

## 4. Walking through cars

Walk mode was documented as "blocked by buildings and vehicles" and was
blocked by buildings: `CityScene::isSolid` asked the layout and nothing
else.

A vehicle is now an oriented box the size of the model actually drawn for
it -- an authored car passes its own length, width and height down to the
simulation, so a Sprinter is a Sprinter to walk into and not a hatchback.
The same list carries everything in the footway with mass: lamp columns,
signal posts, sign posts, mast arms, bollards, bins, hydrants, cabinets,
benches, planters, bike stands, the bus shelter, and every tree up to its
clear stem -- a trunk stops a walker, a crown does not, and a solid the
width of a crown would close the footway.

`pushOut` is the other half. The camera cannot walk into a car, but a car
can still drive into the camera, and without this it would be carried down
the road inside the bodywork. It eases out through the nearest face at
2.4 m/s: brisk enough to clear a stopped car, slow enough that being
clipped by a moving one leaves the camera behind it rather than on its
roof. Entering walk mode inside a car runs a second of it at once, so Tab
cannot strand the camera in a boot.

`traffic_system_tests` checks the box in isolation and over a whole fleet.
`--walkthrough` checks it in the street; see §8.

## 5. Nobody at the wheel

Thirty moving cars, and once the cars are authored models an empty driver's
seat reads instantly through a windscreen at three metres.

Six seated figures, rigid and in one piece. Through glass at a glancing
angle what has to be there is a head, two shoulders and two arms on the
wheel; the skinned figures the footway uses would cost six draws a car for
detail the glass takes away, and there is no seated clip for them anyway.
Two draws each, only inside thirty metres -- past which the car is already
drawn as its welded far copy -- and only in moving cars, because a parked
car is empty.

The seat comes from the vehicle's *class* dimensions rather than the drawn
model's bounding box: the Mini's box is 1.83 m tall because of that aerial,
and a seat placed off it puts a head through the roof. Left-hand drive,
which in this frame is -X, the side the lanes agree is the left of a car
facing north.

`--lineup` seats every car and adds a "Driver N" viewpoint per class, aimed
from `driverSeat` itself so a seat that moves takes its viewpoint with it
(`lineup/driver-saloon.png`). That found two things worth knowing: the
camera yaw convention is `atan2(dx, -dz)`, not `atan2(dx, dz)`, because of
the minus on Z in `Camera::forward`; and a driver behind CNA's glass is lit
as though the glass were not there, since it is a reflection layer and not
a filter, so the driver tints carry the light the screen and the roof would
have taken.

## 6. The lofts among the authored cars

The hero bays were one stretch north of the junction. The rest of the
street kept lofted cars, and a lofted crossover parked between two authored
ones does not read as a cheaper car -- it reads as the moment the rendering
stops. `pairs/07-the-long-view-south.png` is the clearest: a white lofted
van, ten metres from the camera, filling a quarter of the frame.

All forty-four bays within a hundred and twenty-six metres now carry one of
the eight models, and the district beyond the modelled frontage parks their
*far* copies rather than the lofts. That is close to free -- instances of
meshes already uploaded -- with one catch: a level of detail is chosen once
per instance group, so one car three metres away would draw every copy of
that model down the whole street at full detail. The bays are dealt into
four rings of thirty-four metres, and the camera upgrades the ring it is
standing in and no other.

The lofts stay in the simulation and as the fallback for a tree that has
fetched no models.

## 7. The frame from end to end

**Vegetation.** The scanned species stood in the hero corridor and the
generated ones everywhere else. A generated tree beside a scanned one is a
different green and a ball on a stick, and a row of them starting at eighty
metres was the most legible thing in the frame saying where the modelling
stopped. Every pit on the main street now carries one of the three scans,
and the district beyond plants the same three at their far level of detail.
`tree-cliff-before.png` and `tree-cliff-after.png` are the same camera on
either side of that change; the two bright lollipop crowns on the right of
the first are the generated trees.

Two rings again, for the same reason the cars have four.

**Faces.** MakeHuman's skins are colour and nothing else -- no normal map,
no roughness map -- so a head at a metre is a matte painting with eyes
drawn on it. The painting does carry the relief, as luminance: a nostril is
dark because it is a hole, a lip edge because it turns away, an eyebrow
because hair stands proud of skin. High-passing the skin and taking the
gradient of what is left recovers exactly that, and
`scripts/people-atlas.py` derives one per skin. The eyes, which MakeHuman
gives the same 0.85 roughness as a shoe, get 0.12 in the merged ORM map --
a specular glint in a cornea is most of what says a face is alive. And the
hands, which the base mesh leaves flat with the fingers spread and the
palms forward, turn fifty degrees at the wrist so the palm faces the thigh.
`lineup/face-before.png` and `lineup/face-after.png`.

**Cost.** See `performance.md`. The short version: a car was thirty-three
draw calls and is eight, a person was six and is three, the shadow pass is
fitted to its cascades instead of to a disc around the camera, and the
frame went from 81 ms to 60 on the machine's own GPU while the scene grew.

## 8. Driven, not photographed

`--walkthrough <dir>` puts the camera in walk mode and drives it along a
route aimed from the scene itself, through the same `moveWithCollision` and
`escapeSolids` a person's WASD goes through, at a fixed step so two runs
agree. What it reports on this build:

| leg | asked | moved | blocked | closest to a car | inside one | pushed |
|---|---:|---:|---:|---:|---:|---:|
| down the footway | 29.1 m | 28.5 m | 26 steps | 0.78 m | 0 | 0 |
| into a parked car | 8.9 m | 3.1 m | 220 steps | **0.32 m** | **0** | 0 |
| along the kerb past them | 38.4 m | 12.5 m | 1 068 steps | 0.00 m | 5 | 0 |
| standing in a traffic lane | 0 | 12.0 m | 0 | 0.00 m | 262 | 12.0 m |
| watching the traffic | 0 | 0 | 0 | 2.51 m | 0 | 0 |
| at the crossing | 0 | 0 | 0 | 5.11 m | 0 | 0 |

0.32 m is the walking camera's own radius: it walks up to the car and stops
at its skin. The five steps inside one on the kerb leg and the 262 in the
traffic lane are a moving car arriving faster than the 2.4 m/s the camera
is eased out at -- the intended behaviour, and the reason the escape exists.
The camera ends up behind the car, not on its roof; over twenty-six seconds
standing in a live lane it is pushed twelve metres clear and never carried.

The frames are in `walkthrough/`.

## 9. Tried and rejected

- **A fixed margin on the shadow slice.** A cascade only needs casters that
  can throw a shadow into it, and the first version grew the slice by a flat
  twenty-five metres. That let bollards nine metres behind the camera into
  the seven-metre near cascade and made the pass *bigger* -- 3 910 shadow
  draws against 2 622. A caster's reach is its own size, so the margin is
  three times its radius.
- **The slice sphere alone.** It is the right shape and the wrong bound on
  its own: the near cascade's sphere reaches thirty metres sideways where
  the cascade reaches seven. Both tests, and the pass is 2 531.
- **Atlasing a car's paint.** `vehicle-atlas.py` leaves any textured
  material covering more than a fifth of the car alone. On every one of
  these eight that is the body, and the two the closest viewpoints park
  three metres from carry 2k paint on purpose.
- **Merging a person's skin.** It is the one 2048 texture on a figure and
  it is the face. Six parts become three, not two.
- **A mutual push between pedestrians.** Two people each stepping out of
  the other's way is the version of crowd separation that jitters. The
  rules here are one-directional -- a lane target, a follower yielding, an
  ordered sidestep -- and none of them can cycle.
- **`--supersample 2` for the regression pairs.** As in the fifth pass: the
  pairs are at one sample so what they show is the scene and not the
  sampling. Six flagship frames are resolved at two.

## 10. What still gives it away

1. **The context district, walked rather than looked down.** Its blocks
   carry real window openings and its trees and cars are now the street's
   own, but the facades are one flat plane of render with a shopfront band,
   and standing among them at a hundred and fifty metres from the junction
   the massing is plainly cheaper than the modelled frontage. From the
   flagship viewpoints it is a hundred metres away and reads; from inside
   it does not.
2. **A face is still a painting with relief on it.** The derived normal
   gives a nostril depth and a lip an edge, and at a metre that is a large
   improvement on a matte surface, but there is no subsurface term, the
   hair is card sheets, and one figure's fringe hangs through an eye.
3. **A hundred and fifty skinned draws.** A person is three, and a skinned
   draw cannot be instanced because each figure carries its own bone
   palette (CNA-F6 is the same shape of problem in the shadow pass). Fifty
   people is the largest family in the frame by a factor of four, and the
   next thing worth doing about it is fewer *materials* still -- one, with
   the hair alpha-tested against the same atlas -- or a way to instance a
   palette, which is the framework's.

# Before and after, the eighth pass

Every "before" is HEAD `9ac6ba2`, the end of the seventh pass, rendered by
that build; every "after" is the same camera on this pass's final build.
All pairs are 1600 x 900 at one sample, `--no-overlay`, the shipped
settings, the default seed, the same frame of the fixed-step clock -- so the
traffic and the crowd stand in the same places on both sides and what
differs is the scene. Nothing here is supersampled.

## The cameras

The approach test the brief asked for: walk toward the district and find
the distance at which it gives itself away. Each camera is a `--camera`
argument (x, y, z, yaw, pitch; yaw 0 looks south along -Z, pi north) so
the pair can be re-shot by anyone.

| pair | camera | what it shows |
|---|---|---|
| `district-approach-60` | `-7.4,1.80,76.0,3.14159,0.00` | on the west footway, 60 m short of the first district block, the modelled frontage running out ahead |
| `district-approach-30` | `-7.4,1.80,106.0,3.14159,0.00` | 30 m short |
| `district-approach-10` | `-7.4,1.80,126.0,3.14159,0.00` | 10 m short: the last modelled shop at the right shoulder, the district filling the frame |
| `district-inside` | `-7.4,1.80,170.0,3.14159,0.00` | standing in the district, its blocks from 5 to 60 m |
| `district-across` | `-7.4,1.80,175.0,1.87080,0.04` | across the street at the opposite row, 20 m |
| `district-elevated` | `-40.0,30.00,200.0,0.67500,-0.30` | from a window over the west blocks: roofs, corners, the painted rows behind |
| `district-behind` | `0.0,45.00,150.0,5.21000,-0.45` | the seventh pass's proving camera, 45 m over the north arm looking back across the west blocks |
| `side-street-district` | `70.0,1.80,-4.4,1.57080,0.00` | the side street's own district, from its first block |
| `viewpoint-1` | named viewpoint 1 | the flagship footway view, for regression |
| `viewpoint-6` | named viewpoint 6 | above the junction: city depth |
| `viewpoint-7` | named viewpoint 7 | the long view south |

`pairs/<name>-before.png` and `pairs/<name>-after.png` hold the frames. The
three that prove the mid tier -- `district-approach-10`, `district-inside`
and `district-across` -- are kept at their full 1600 x 900; the other eight
pairs are the same frames resampled to 1024 x 576, both sides alike, to
keep the set under twenty-five megabytes.

## The approach

**`district-approach-60`** -- identical on both sides but for the far end
of the street: at sixty metres the modelled frontage still fills the frame
and the district is a strip between the trees. The cars, the people and the
shadows stand in the same places on both sides, which is the fixed-step
clock doing its job.

**`district-approach-30`** -- the first frame where the district is read.
Before: beyond the tree, one cream slab of building on the left with a row
of identical windows. After: a terracotta plot with two storeys of
balconies, a buff brick plot, a cream one, each a different height, each
with a cornice. From thirty metres a viewer can tell the district is *made
of buildings* rather than of blocks.

**`district-approach-10`** -- the seventh pass's own failure distance.
Before: the flat cream block with flat crosses for frames and no lintels,
the whole block one building, its shopfront a band. After: the same
frontage as a terrace of plots -- terracotta with balconies and blue
shutters, buff brick with framed sashes and heads, a cream plot with its
own cornice -- and the corner plot's end, which faces the first cross
street, is an elevation with windows in it rather than a blank wall. The
transition from the last modelled shop at the right shoulder to the first
district plot on the left is now a change of *detail density*, not a change
of *kind*.

**`district-inside`** -- standing among them. Before: a brick block on the
left with stickers for windows, a shopfront band, a plain wall above. After:
a terrace on the left of brick, ochre-with-shutters and terracotta plots,
each with real reveals, sills and heads; a shopfront with mullions and
pilasters on the right; roofs of different heights against the sky. This is
the frame that answers the brief's question: at five to fifteen metres the
district plots reveal what they are not -- no dressed rooms behind the
glass, no bevelled arrises, no weathering -- but they no longer reveal
themselves as a different *generation* of architecture.

**`district-across`** -- twenty metres across the street at the opposite
row. Before: one green-grey block with flat windows under a tree. After: a
grey plot with framed sashes and sills, a red-brick plot a storey lower
with a flat roof, an ochre plot with balconies and shutters a storey higher,
pilasters between them, plinths under the shopfronts. The best single frame
for the mid tier.

**`district-elevated`** -- over the west blocks. After: per-plot roofs,
pitched with stacks and ridges along the street beside flat felt ones,
party walls showing above lower neighbours, the painted rows behind with
their new cornice bands. The corners that used to be blank are elevations.

**`district-behind`** -- the seventh pass's proving camera. Both sides show
a city behind the frontage; the after side's painted rows carry a white
cornice band at the eaves and pilasters dividing the long faces, so the
first row behind the frontage no longer reads as one printed plane where a
cross street opens a view of it. The rows themselves are laid out
differently -- the district's random stream moved when the blocks became
plots -- which is expected and not a regression.

**`side-street-district`** -- the side street's own district from its
first block. Before: a blank ochre wall running the length of the left
side with a plinth and nothing else, a cream block with stickers for
windows on the right. After: a shopfront with mullions and pilasters on
the left under a dark fascia; on the right an ashlar plot with framed
sashes and a door in a recess, a red-brick plot beside it a storey
taller, a buff plot beyond, each with its own cornice line, and the yellow
and cream plots further down. The side street reads as a street rather
than a corridor between two slabs.

## The named viewpoints

**`viewpoint-1`** -- the flagship footway view. No change wanted here and
none made: the two frames are the same picture to the pixel in everything
nearer than the vanishing point, where the district's first plots replace
the seventh pass's first blocks at a size of a few pixels.

**`viewpoint-6`** -- above the junction. The near roofscape is the same to
the pixel; beyond the modelled frontage the district's blocks are now
terraces of plots with stepped rooflines -- pitched beside flat, a storey
up and a storey down -- where they were one roof per block, and the painted
rows behind them carry their cornice bands. City depth is as the seventh
pass left it, with a texture to the mid ground it did not have.

**`viewpoint-7`** -- the long view south. Identical: the district is behind
this camera and the far end of the southern arm is hidden by the trees, so
the frame shows that nothing nearer changed -- the hydrant on the footway
casts the same shadow from its generated twin as it did from its 6 200
scanned triangles.

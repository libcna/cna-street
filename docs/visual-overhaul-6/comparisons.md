# Before and after

Every pair below is the same viewpoint, the same seed and the same
resolution (1024 × 576), rendered by the same command with the same
settings. BEFORE is `6a40427`, the last commit before this pass -- the
images the fifth pass committed as its own `after/`, which is exactly the
state this pass started from. AFTER is the current tree.

```
xvfb-run -a ./build/bin/cna-street --capture <dir> --width 1024 --height 576 --no-overlay
```

Nothing in the pairs differs but the content: the camera, the clock, the
seed, the light and the anti-aliasing are identical, and the supersampled
resolve is *not* used, so what they show is the scene and not the sampling.

| # | Viewpoint | Before | After |
|---|-----------|--------|-------|
| 1 | Footway looking south to the junction | ![](before/01-footway-looking-south-to-the-junction.png) | ![](after/01-footway-looking-south-to-the-junction.png) |
| 2 | On the crossing | ![](before/02-on-the-crossing.png) | ![](after/02-on-the-crossing.png) |
| 3 | The corner block | ![](before/03-the-corner-block.png) | ![](after/03-the-corner-block.png) |
| 4 | Down the side street | ![](before/04-down-the-side-street.png) | ![](after/04-down-the-side-street.png) |
| 5 | Looking up at the façades | ![](before/05-looking-up-at-the-facades.png) | ![](after/05-looking-up-at-the-facades.png) |
| 6 | Above the junction | ![](before/06-above-the-junction.png) | ![](after/06-above-the-junction.png) |
| 7 | The long view south | ![](before/07-the-long-view-south.png) | ![](after/07-the-long-view-south.png) |
| 8 | Shopfronts, close | ![](before/08-shopfronts-close.png) | ![](after/08-shopfronts-close.png) |
| 9 | Car, three metres | ![](before/09-car-three-metres.png) | ![](after/09-car-three-metres.png) |
| 10 | Pedestrian, four metres | ![](before/10-pedestrian-four-metres.png) | ![](after/10-pedestrian-four-metres.png) |
| 11 | Shop window | ![](before/11-shop-window.png) | ![](after/11-shop-window.png) |
| 12 | Road surface | ![](before/12-road-surface.png) | ![](after/12-road-surface.png) |
| 13 | Street tree | ![](before/13-street-tree.png) | ![](after/13-street-tree.png) |
| 14 | Façade detail | ![](before/14-facade-detail.png) | ![](after/14-facade-detail.png) |
| 15 | Kerbside | ![](before/15-kerbside.png) | ![](after/15-kerbside.png) |
| 16 | Corner to corner | ![](before/16-corner-to-corner.png) | ![](after/16-corner-to-corner.png) |
| 17 | Pavement cafe | ![](before/17-pavement-cafe.png) | ![](after/17-pavement-cafe.png) |
| 18 | Covered car | ![](before/18-covered-car.png) | ![](after/18-covered-car.png) |

`pairs/` holds each of those as one image, before on the left and after on
the right, which is the form the differences are easiest to see in.

## The five that matter most

Ranked by the fraction of pixels that changed, the two biggest are 15 at
33.9 per cent and 7 at 28.6. The five below are chosen for what they show
rather than for how much of them moved.

1. **`pairs/07-the-long-view-south.png`** — a white lofted van filling a
   quarter of the frame at ten metres becomes a blue VAZ estate, and every
   car down the street behind it becomes an authored one. This is the
   single clearest picture of what the pass did to the traffic.
2. **`pairs/04-down-the-side-street.png`** — a face at two metres. On the
   left it is flat: a painted surface with a nose drawn on it, and two
   hands held out palms forward with the fingers spread. On the right the
   brow, the nose and the jaw have relief, and the hands hang.
3. **`pairs/16-corner-to-corner.png`** — the junction. The crowd on the far
   footway stands apart instead of in a knot, and the woman in the
   foreground has both of the changes in 4.
4. **`pairs/15-kerbside.png`** — the row of parked cars and the trees over
   it, both now the same generation from the kerb to the junction.
5. **`pairs/01-footway-looking-south-to-the-junction.png`** — the flagship
   frame, end to end: authored traffic, authored parking, scanned trees the
   whole length.

## The one that shows the eighty-metre cliff is gone

**`tree-cliff-before.png`** and **`tree-cliff-after.png`**: the same camera
on the west footway at z = −52, looking south down the row of pits, taken
either side of the vegetation change and nothing else. In the first, the
two bright emerald lollipop crowns on the right — a ball on a stick, in a
green nothing else in the frame is — are the generated trees that began
where the hero corridor ended. In the second every tree down the street is
one of the three scans.

## Behaviour

`walkthrough/` holds one frame per leg of `--walkthrough`, which is what a
person walking the street meets rather than what a camera on a tripod sees:

| | |
|---|---|
| ![](walkthrough/01-down-the-footway.png) | ![](walkthrough/02-into-a-parked-car.png) |
| the footway, walked | stopped 0.32 m short of the bodywork |
| ![](walkthrough/04-standing-in-the-traffic-lane.png) | ![](walkthrough/05-watching-the-traffic-from-the-kerb.png) |
| pushed clear of a live lane, not carried | the traffic, from the kerb |
| ![](walkthrough/06-at-the-crossing.png) | ![](lineup/driver-saloon.png) |
| a queue at the kerb, not a heap | somebody at the wheel |

## On the desktop

![](live-on-the-desktop.png)

`live-on-the-desktop.png` is the application running in a window on the
machine's own display, on its own Radeon 780M, with the overlay on: 1 280 ×
720, twenty frames a second, 1 442 draws, 2 551 shadow draws, 7.72 million
triangles, 1 721 batches, 447 textures, 882 MiB, built in 22 seconds.

Driving it from a script was not possible in this session -- the desktop is
Wayland, the window is an Xwayland client, and the compositor will not let
`xdotool` take its focus, so synthetic keystrokes never reach it. That is
what `--walkthrough` is for: it drives the walking camera through the same
`moveWithCollision` and `escapeSolids` that WASD goes through, at a fixed
step, and reports numbers rather than impressions.

## The line-up

| before | after | |
|---|---|---|
| ![](lineup/stride-before.png) | ![](lineup/stride-after.png) | one figure at heel strike, where a stance is widest |
| ![](lineup/face-before.png) | ![](lineup/face-after.png) | the same face at a metre, before and after the derived relief, the glossy eye and the turned wrist |

`vehicle-orientation.png` is the eight authored cars in side view with the
nose expected to the right, drawn by `scripts/vehicle-orientation.py`
straight out of the exported files.

## Flagship

`flagship/` holds ten frames at 1920 × 1080 at the pairs' settings, and six
of them again through `--supersample 2` (`vpN-ss2.png`), which is the
resolve the README's screenshots are shot with.

# Which end of a car is its nose, from the body's shape alone.
#
# Imported by scripts/blender-vehicles.py, which uses it to *decide* the pose a
# derived model is exported in, and by scripts/vehicle-orientation.py, which
# uses it to *check* the exported files. One rule in one place, because the
# alternative -- a hand-set `flip` flag in the pipeline's table and nothing
# testing it -- is what shipped a car that drove permanently in reverse.
#
# The rule reads two properties every passenger car and van has, whatever its
# body style:
#
#   cabin  the tall part of it -- roof, glasshouse, box -- sits behind the
#          middle, because the engine and the bonnet are ahead of it;
#   deck   the deck ahead of the windscreen is longer than the deck behind the
#          backlight.
#
# Both are measured off a height profile: the top of the body over each thin
# slab of its length. The profile is **weighted by surface area**, and that is
# the whole difference between a rule that works and one that does not. The
# first version took the largest vertex height per slab, and the Mini's roof
# aerial -- a 60 cm rod of a few hundred triangles and about a fiftieth of a
# square metre -- became "the tallest part of the car", at the back, so the
# rule turned the Mini round and it drove backwards down Lindenstrasse. An
# aerial, a mirror stalk, a wiper and a roof rail all have height and no area;
# a roof has both.
import numpy as np


def area_profile(along, up, area, slabs=80):
    """The top of the body over each slab of its length, area-weighted.

    @p along, @p up and @p area are per-triangle: the centroid's coordinate
    along the car, the centroid's height, and the triangle's area. Each slab's
    height is the level under which 96 per cent of that slab's area lies, so a
    thin tall thing cannot raise it.
    """
    lo, hi = float(along.min()), float(along.max())
    if hi - lo < 1e-6:
        return None, None, 0.0, 0.0
    edges = np.linspace(lo, hi, slabs + 1)
    which = np.clip(np.digitize(along, edges) - 1, 0, slabs - 1)
    profile = np.full(slabs, np.nan)
    for slab in range(slabs):
        pick = which == slab
        if not pick.any():
            continue
        heights = up[pick]
        weights = area[pick]
        order = np.argsort(heights)
        heights = heights[order]
        cumulative = np.cumsum(weights[order])
        if cumulative[-1] <= 0.0:
            continue
        profile[slab] = float(heights[np.searchsorted(cumulative, 0.96 * cumulative[-1])])
    centres = 0.5 * (edges[:-1] + edges[1:])
    return profile, centres, lo, hi


def front_score(along, up, area, slabs=80):
    """How strongly the nose points toward the *negative* end of @p along.

    Returns (score, cabin_vote, deck_vote), each in -1..1. A score of zero or
    more means the nose is at the negative end. The two votes are independent
    and, on the eight cars this project carries, they agree on every one.
    """
    profile, centres, lo, hi = area_profile(along, up, area, slabs)
    if profile is None:
        return 0.0, 0.0, 0.0
    filled = np.isfinite(profile)
    if filled.sum() < 8:
        return 0.0, 0.0, 0.0
    height = float(np.nanmax(profile))
    floor = float(np.nanmin(profile))
    span = max(height - floor, 1e-6)

    tall = filled & (profile - floor > 0.80 * span)
    half = max(0.5 * (hi - lo), 1e-6)
    cabin = float(np.average(centres[tall])) if tall.any() else 0.5 * (lo + hi)
    # The cabin sits away from the nose, so a cabin toward +along votes for a
    # nose toward -along.
    cabin_vote = float(np.clip((cabin - 0.5 * (lo + hi)) / half, -1.0, 1.0))

    low = filled & (profile - floor < 0.70 * span)

    def run(order):
        count = 0
        for slab in order:
            if not filled[slab]:
                continue
            if not low[slab]:
                break
            count += 1
        return count

    fore = run(range(slabs))                  # low deck in from the -along end
    aft = run(range(slabs - 1, -1, -1))       # low deck in from the +along end
    deck_vote = float(np.clip((fore - aft) / max(fore + aft, 1), -1.0, 1.0))
    return 0.5 * cabin_vote + 0.5 * deck_vote, cabin_vote, deck_vote


def triangle_centroids(points, indices):
    """Per-triangle centroid and area for a mesh given as points and indices."""
    tri = np.asarray(indices).reshape(-1, 3)
    a = points[tri[:, 0]]
    b = points[tri[:, 1]]
    c = points[tri[:, 2]]
    centroid = (a + b + c) / 3.0
    cross = np.cross(b - a, c - a)
    return centroid, 0.5 * np.linalg.norm(cross, axis=1)

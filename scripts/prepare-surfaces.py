#!/usr/bin/env python3
"""Turn fetched PBR texture sets into the content pipeline's source images.

The material catalogue generates every surface from a seed, and the content
build bakes those to PNG and compiles them. This is the second source of
surfaces: scanned, photographed PBR sets with a clean licence, declared one at
a time in `assets/external/manifest.json` under "surfaces", fetched by
`scripts/fetch-assets.sh` and turned here into the three maps the runtime
asks for, under the *catalogue* name they replace:

    <name>.albedo.png   sRGB colour
    <name>.normal.png   tangent-space normal, green along +v (the DirectX
                        convention this project's meshes and generator use)
    <name>.orm.png      R occlusion, G roughness, B metalness

It writes them into the content staging directory *after* the bake has
written the generated ones, so where a scanned surface exists it wins and
where it does not the generated one stands -- which is the same contract the
compiled content has always had with the generators.

It also writes `authored.txt`: one line per replaced surface, carrying the UV
scale that maps the geometry's tile size onto the scan's physical size. The
runtime reads it to (a) apply that scale through `KHR_texture_transform`, and
(b) take the roughness and metalness in the map at face value instead of
normalising a declared factor against a generator's mean.

Runs without a GPU and without the build; needs Pillow. A tree without Pillow
gets the generated surfaces and a warning, not a failed build.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--downloads", required=True, type=pathlib.Path,
                        help="assets/external/downloads")
    parser.add_argument("--staging", required=True, type=pathlib.Path,
                        help="where the bake wrote its PNGs; the scans are written beside them")
    parser.add_argument("--only", default="",
                        help="comma-separated surface names to prepare (default: every one)")
    args = parser.parse_args()

    try:
        from PIL import Image, ImageOps  # noqa: F401
    except ImportError:
        print("prepare-surfaces: Pillow is not installed; the generated surfaces stand",
              file=sys.stderr)
        return 0

    manifest = json.loads(args.manifest.read_text())
    surfaces = manifest.get("surfaces", [])
    only = {s for s in args.only.split(",") if s}
    args.staging.mkdir(parents=True, exist_ok=True)

    authored: list[str] = []
    written = 0
    skipped = 0
    for surface in surfaces:
        name = surface["name"]
        if only and name not in only:
            continue
        try:
            scale = prepare(surface, args.downloads, args.staging)
        except FileNotFoundError as missing:
            # Not fetched. The generated surface stands, exactly as a model that
            # was not fetched leaves a bare plinth.
            print(f"  {name:<24} not fetched ({missing.filename}); generated surface stands")
            skipped += 1
            continue
        keep = 1 if surface.get("keepTint", False) else 0
        authored.append(f"{name}\t{scale[0]:.6f}\t{scale[1]:.6f}\t{keep}")
        written += 1

    if authored:
        lines = [
            "# cna-street authored surfaces: name <tab> uvScaleU <tab> uvScaleV <tab> keepTint",
            "# A surface listed here came from a scanned PBR set rather than a generator.",
            "# MaterialLibrary applies the UV scale through KHR_texture_transform so the",
            "# scan's physical size matches the geometry's tile, takes the roughness and",
            "# metalness in the map at face value, and resets the base colour to white",
            "# unless keepTint is 1 (a scan neutralised so the catalogue can tint it).",
            "# Written by scripts/prepare-surfaces.py.",
        ]
        (args.staging / "authored.txt").write_text("\n".join(lines + authored) + "\n")

    print(f"prepare-surfaces: {written} scanned surface(s) written, {skipped} not fetched")
    return 0


def prepare(surface: dict, downloads: pathlib.Path, staging: pathlib.Path) -> tuple[float, float]:
    from PIL import Image, ImageOps

    name = surface["name"]
    size = int(surface.get("size", 1024))
    files = surface["files"]
    folder = downloads / surface["folder"]

    def load(role: str, mode: str = "RGB") -> Image.Image:
        path = folder / files[role]
        image = Image.open(path).convert(mode)
        if image.size != (size, size):
            image = image.resize((size, size), Image.LANCZOS)
        return image

    # --- albedo ----------------------------------------------------------
    albedo = load("albedo")
    if surface.get("neutralise", False):
        albedo = neutralise(albedo, float(surface.get("neutralLuminance", 0.80)))
    tint = surface.get("tint")
    if tint:
        albedo = multiply(albedo, tint)
    if surface.get("desaturate", 0.0):
        grey = ImageOps.grayscale(albedo).convert("RGB")
        albedo = Image.blend(albedo, grey, float(surface["desaturate"]))

    # --- normal ----------------------------------------------------------
    # The street's own meshes carry a tangent frame whose bitangent runs along
    # +v -- down the image -- so the green channel of a normal map has to
    # point the same way: the DirectX convention, which is also what the
    # catalogue's generator writes. Poly Haven publishes OpenGL-convention
    # maps (green up the image), so those are inverted here. This was settled
    # by rendering a map of hemispherical bumps under a low sun and seeing
    # them come out as bowls the other way round; see docs/design-notes.md.
    normal = load("normal")
    if surface.get("normalConvention", "opengl") == "opengl":
        r, g, b = normal.split()
        normal = Image.merge("RGB", (r, ImageOps.invert(g), b))
    if surface.get("normalStrength", 1.0) != 1.0:
        normal = scale_normal(normal, float(surface["normalStrength"]))

    # --- ORM -------------------------------------------------------------
    if "orm" in files:
        orm = load("orm")
    else:
        occlusion = load("occlusion", "L") if "occlusion" in files else Image.new("L", (size, size), 255)
        roughness = load("roughness", "L")
        metalness = load("metalness", "L") if "metalness" in files else Image.new("L", (size, size), 0)
        orm = Image.merge("RGB", (occlusion, roughness, metalness))
    bias = float(surface.get("roughnessBias", 0.0))
    gain = float(surface.get("roughnessScale", 1.0))
    occlusion_scale = float(surface.get("occlusionScale", 1.0))
    if bias != 0.0 or gain != 1.0 or occlusion_scale != 1.0:
        r, g, b = orm.split()
        g = g.point(lambda v: max(0, min(255, int(round(v * gain + bias * 255.0)))))
        # An interior surface sees almost no sky: the catalogue scales the
        # occlusion channel down for the rooms behind the shop glass, and a
        # scanned floor laid in one of those rooms has to be scaled the same.
        r = r.point(lambda v: max(0, min(255, int(round(v * occlusion_scale)))))
        orm = Image.merge("RGB", (r, g, b))

    albedo.save(staging / f"{name}.albedo.png")
    normal.save(staging / f"{name}.normal.png")
    orm.save(staging / f"{name}.orm.png")
    # A scanned surface has no emissive of its own; a stale generated one must
    # not survive beside it or the runtime would load the generator's glow
    # over the scan. The exception is an interior surface, whose room light
    # the catalogue bakes as an emissive copy of the albedo ("warm light
    # bouncing off this colour"), which a scan laid in that room needs too.
    stale = staging / f"{name}.emissive.png"
    if surface.get("emissiveFromAlbedo", False):
        albedo.save(stale)
    elif stale.exists():
        stale.unlink()

    physical = surface["physicalMetres"]
    tile = surface["tileMetres"]
    if not isinstance(tile, list):
        tile = [tile, tile]
    scale = (float(tile[0]) / float(physical), float(tile[1]) / float(physical))
    print(f"  {name:<24} {size}px  {physical} m scan over a {tile[0]} x {tile[1]} m tile"
          f"  -> uv scale {scale[0]:.3f} x {scale[1]:.3f}")
    return scale


def neutralise(albedo, target_luminance: float):
    """Divide the colour out of a scan so the catalogue can tint it.

    The catalogue's render, painted-metal and fabric surfaces are *white*: all
    the pattern and none of the colour, with the colour arriving as the
    material's base colour so seven facade colours cost one texture. A scanned
    plaster arrives cream or grey. Dividing every texel by the image's mean
    colour keeps the pattern -- the trowel marks, the dirt, the hairline
    cracks, all of it as ratios to the mean -- and removes the colour cast,
    then the result is scaled so its mean luminance is `target_luminance`,
    which is about what a fresh pale render measures. Done in linear light,
    because a ratio of sRGB-encoded values is not a ratio of reflectances.
    """
    import numpy as np

    srgb = np.asarray(albedo).astype(np.float32) / 255.0
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    mean = linear.reshape(-1, 3).mean(axis=0)
    mean = np.maximum(mean, 1e-4)
    neutral = linear / mean
    luminance = neutral @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    neutral = neutral * (target_luminance / max(float(luminance.mean()), 1e-4))
    neutral = np.clip(neutral, 0.0, 1.0)
    encoded = np.where(neutral <= 0.0031308, neutral * 12.92,
                       1.055 * np.power(neutral, 1.0 / 2.4) - 0.055)
    from PIL import Image
    return Image.fromarray((np.clip(encoded, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGB")


def multiply(albedo, tint):
    import numpy as np
    from PIL import Image
    srgb = np.asarray(albedo).astype(np.float32) / 255.0
    linear = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
    linear = np.clip(linear * np.array(tint, dtype=np.float32), 0.0, 1.0)
    encoded = np.where(linear <= 0.0031308, linear * 12.92,
                       1.055 * np.power(linear, 1.0 / 2.4) - 0.055)
    return Image.fromarray((np.clip(encoded, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGB")


def scale_normal(normal, strength: float):
    """Flatten or steepen a normal map by scaling its tangent components."""
    import numpy as np
    from PIL import Image
    n = np.asarray(normal).astype(np.float32) / 255.0 * 2.0 - 1.0
    n[..., 0] *= strength
    n[..., 1] *= strength
    n[..., 2] = np.maximum(n[..., 2], 1e-3)
    length = np.sqrt((n * n).sum(axis=2, keepdims=True))
    n = n / np.maximum(length, 1e-6)
    return Image.fromarray(((n * 0.5 + 0.5) * 255.0 + 0.5).astype(np.uint8), "RGB")


if __name__ == "__main__":
    sys.exit(main())

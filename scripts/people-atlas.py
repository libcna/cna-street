# Consolidates a derived person's parts so that a pedestrian is three draws.
#
#     python3 scripts/people-atlas.py [--dir DIR] [--report] [name ...]
#
# A skinned draw cannot be instanced -- every figure carries its own bone
# palette -- so a person costs one draw call per *material*, whatever the
# distance. `scripts/blender-people.py` writes six: body, eyes, eyebrows,
# hair, the suit and the shoes. Fifty-eight people on the street is three
# hundred draw calls, which was the largest single family in the frame by a
# factor of five, and the frame is bound by draw submission.
#
# This merges the parts that can share a material into one, exactly as
# scripts/vehicle-atlas.py does for a car: an atlas per channel, a cell per
# part, the UVs remapped into the cell, and the vertices and indices
# concatenated. What each part had as a *factor* -- the tint on a coat, the
# roughness of a shoe -- goes into the atlas, roughness and metalness into the
# green and blue of an ORM map, so nothing is averaged away.
#
# Two rules decide what stays apart:
#
#   * the alpha mode and the sidedness. Hair and eyebrows are alpha-masked and
#     double-sided; a body is neither, and merging them would put a cutout
#     through a face.
#   * the skin. It is the one 2048 texture on a person and it is the face, the
#     thing a viewer looks at hardest and the thing this project has least
#     under control; putting it through a 512-pixel cell to save one draw call
#     is not a trade worth making.
#
# So six parts become three: the face, everything else opaque, and the hair.
# Both levels of detail share one set of atlases, because the far level is the
# near one decimated -- same materials, same UV layout -- so the merge costs
# no texture memory over what the six separate parts used, and rather less.
import argparse
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PEOPLE = os.path.join(ROOT, "assets", "external", "downloads", "derived", "people")

# 17 little-endian floats: position, normal, tangent, texture coordinate,
# blend weights, and the four blend indices packed into the last one.
VERTEX_BYTES = 17 * 4
UV_OFFSET = 10 * 4
# The cell a merged part is scaled into, and the albedo size above which a
# part keeps its own draw. A person is two hundred pixels tall at the distance
# a street is seen from; a sleeve is thirty of them.
CELL = 512
KEEP_APART_ABOVE = 1024


def log(*args):
    print("people:", *args, flush=True)


def texture(directory, name, mode, size=None, fallback=(255, 255, 255, 255)):
    """One of a part's images, or a one-pixel stand-in."""
    if name:
        path = os.path.join(directory, name + ".png")
        if os.path.isfile(path):
            return Image.open(path).convert(mode)
    return Image.new(mode, size or (1, 1), fallback[:len(mode)])


def paste_cell(atlas, image, x, y, size, pad):
    inner = size - 2 * pad
    scaled = image.resize((inner, inner), Image.LANCZOS)
    block = Image.new(image.mode, (size, size))
    block.paste(scaled, (pad, pad))
    left = scaled.crop((0, 0, 1, inner)).resize((pad, inner), Image.NEAREST)
    right = scaled.crop((inner - 1, 0, inner, inner)).resize((pad, inner), Image.NEAREST)
    block.paste(left, (0, pad))
    block.paste(right, (size - pad, pad))
    top = block.crop((0, pad, size, pad + 1)).resize((size, pad), Image.NEAREST)
    bottom = block.crop((0, size - pad - 1, size, size - pad)).resize((size, pad), Image.NEAREST)
    block.paste(top, (0, 0))
    block.paste(bottom, (0, size - pad))
    atlas.paste(block, (x, y))


def tint(image, colour):
    """Folds a part's baseColour factor into its albedo."""
    if tuple(colour) == (1.0, 1.0, 1.0):
        return image
    pixels = np.asarray(image, dtype=np.float32).copy()
    for channel in range(3):
        pixels[..., channel] *= float(colour[channel])
    return Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8), image.mode)


def part_key(part):
    return (part.get("alphaMode", "OPAQUE"), bool(part.get("doubleSided", False)))


def read_part(blob, part):
    offset = int(part["vertexOffset"])
    count = int(part["vertexCount"])
    vertices = np.frombuffer(blob, dtype=np.uint8, count=count * VERTEX_BYTES,
                             offset=offset).reshape(count, VERTEX_BYTES).copy()
    indices = np.frombuffer(blob, dtype="<u4", count=int(part["indexCount"]),
                            offset=int(part["indexOffset"])).copy()
    return vertices, indices


def remap_uv(vertices, x, y, inner, size):
    """Rewrites the texture coordinate of every vertex into an atlas cell.

    Byte-wise, because the seventeenth float of a vertex is four bone indices
    reinterpreted as a float and reading it as a number risks changing it.
    """
    uv = vertices[:, UV_OFFSET:UV_OFFSET + 8].copy().view("<f4").reshape(-1, 2)
    uv = np.clip(uv, 0.0, 1.0)
    uv = np.stack([(x + uv[:, 0] * inner) / size, (y + uv[:, 1] * inner) / size], axis=1)
    vertices[:, UV_OFFSET:UV_OFFSET + 8] = uv.astype("<f4").view(np.uint8).reshape(-1, 8)
    return vertices


def consolidate(directory, name, report=False):
    header_path = os.path.join(directory, name + ".json")
    header = json.load(open(header_path))
    if header.get("consolidated"):
        log(f"{name}: already consolidated")
        return False

    levels = [level for level in ("near", "far") if level in header]
    if not levels:
        return False
    reference = header[levels[0]]["parts"]

    # --- who merges with whom, decided once and applied to every level ----
    groups = {}
    for index, part in enumerate(reference):
        albedo = texture(directory, part.get("albedo"), "RGBA")
        if max(albedo.size) > KEEP_APART_ABOVE:
            groups.setdefault(("skin", index), []).append(index)
            continue
        groups.setdefault(part_key(part), []).append(index)

    if report:
        log(f"{name}: {len(reference)} parts -> {len(groups)}")
        for key, members in groups.items():
            log(f"    {key}: " + ", ".join(reference[i].get("kind", "?") for i in members))

    if len(groups) >= len(reference):
        log(f"{name}: nothing to merge")
        return False

    # --- one atlas per merged group, shared by both levels ----------------
    atlases = {}
    for key, members in groups.items():
        if len(members) < 2:
            continue
        columns = 1
        while columns * columns < len(members):
            columns += 1
        cell = min(CELL, 2048 // columns)
        pad = max(2, cell // 64)
        size = cell * columns
        albedo_atlas = Image.new("RGBA", (size, size), (255, 255, 255, 255))
        normal_atlas = Image.new("RGB", (size, size), (128, 128, 255))
        orm_atlas = Image.new("RGB", (size, size), (255, 255, 0))
        cells = {}
        for slot, index in enumerate(members):
            part = reference[index]
            albedo = tint(texture(directory, part.get("albedo"), "RGBA"),
                          part.get("baseColour", [1.0, 1.0, 1.0]))
            normal = texture(directory, part.get("normal"), "RGB", fallback=(128, 128, 255))
            rough = int(round(float(part.get("roughness", 1.0)) * 255.0))
            metal = int(round(float(part.get("metallic", 0.0)) * 255.0))
            orm = Image.new("RGB", (1, 1), (255, min(255, rough), min(255, metal)))
            x = (slot % columns) * cell
            y = (slot // columns) * cell
            paste_cell(albedo_atlas, albedo, x, y, cell, pad)
            paste_cell(normal_atlas, normal, x, y, cell, pad)
            paste_cell(orm_atlas, orm, x, y, cell, pad)
            cells[index] = (x + pad, y + pad, cell - 2 * pad)
        tag = f"{name}-{key[0].lower()}{'-two' if key[1] else ''}"
        albedo_atlas.save(os.path.join(directory, f"{tag}.albedo.png"), optimize=True)
        normal_atlas.save(os.path.join(directory, f"{tag}.normal.png"), optimize=True)
        orm_atlas.save(os.path.join(directory, f"{tag}.orm.png"), optimize=True)
        atlases[key] = (tag, cells, size)

    # --- rewrite each level ----------------------------------------------
    for level in levels:
        entry = header[level]
        blob = open(os.path.join(directory, entry["file"]), "rb").read()
        out = bytearray()
        parts = []
        for key, members in groups.items():
            vertices = []
            indices = []
            base = 0
            for index in members:
                part = entry["parts"][index]
                block, strip = read_part(blob, part)
                if key in atlases:
                    _tag, cells, size = atlases[key]
                    x, y, inner = cells[index]
                    block = remap_uv(block, x, y, inner, size)
                vertices.append(block)
                indices.append(strip + base)
                base += block.shape[0]
            block = np.concatenate(vertices)
            strip = np.concatenate(indices).astype("<u4")
            first = reference[members[0]]
            while len(out) % 4:
                out.append(0)
            vertex_offset = len(out)
            out.extend(block.tobytes())
            index_offset = len(out)
            out.extend(strip.tobytes())
            merged = {
                "kind": first.get("kind", "body") if len(members) == 1 else "merged",
                "albedo": first.get("albedo"),
                "normal": first.get("normal"),
                "orm": None,
                "baseColour": first.get("baseColour", [1.0, 1.0, 1.0]),
                "roughness": first.get("roughness", 1.0),
                "metallic": first.get("metallic", 0.0),
                "alphaMode": first.get("alphaMode", "OPAQUE"),
                "doubleSided": bool(first.get("doubleSided", False)),
                "vertexOffset": vertex_offset,
                "vertexCount": int(block.shape[0]),
                "indexOffset": index_offset,
                "indexCount": int(strip.shape[0]),
            }
            if key in atlases:
                tag = atlases[key][0]
                merged.update({"albedo": f"{tag}.albedo", "normal": f"{tag}.normal",
                               "orm": f"{tag}.orm", "baseColour": [1.0, 1.0, 1.0],
                               "roughness": 1.0, "metallic": 1.0})
            parts.append(merged)
        with open(os.path.join(directory, entry["file"]), "wb") as handle:
            handle.write(bytes(out))
        entry["parts"] = parts

    header["consolidated"] = True
    with open(header_path, "w") as handle:
        json.dump(header, handle, indent=1)
    log(f"{name}: {len(reference)} parts -> {len(header[levels[0]]['parts'])}")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", dest="directory", default=PEOPLE)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    if not os.path.isdir(args.directory):
        log("no derived people: nothing to consolidate")
        return 0
    names = args.names or sorted(
        os.path.splitext(f)[0] for f in os.listdir(args.directory) if f.endswith(".json"))
    done = 0
    for name in names:
        if consolidate(args.directory, name, args.report):
            done += 1
    log(f"{done} of {len(names)} consolidated")
    return 0


sys.exit(main())

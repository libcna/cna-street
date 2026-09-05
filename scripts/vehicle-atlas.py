# Consolidates a derived car's materials so that a car is a handful of draws.
#
#     python3 scripts/vehicle-atlas.py [--in DIR] [--out DIR] [--report] [name ...]
#
# The problem this exists for. An authored car arrives from its author with
# one material per *part*, because that is how a modeller works: bodypaint,
# chrome, plastic, rubber_masks, aluminum, red_chrome, unpaint_black,
# calipers, black_paint, brake_disk, blackness, 20_-_Default. Fifteen on the
# Astra, twenty on the small price car. CNA draws one call per material per
# mesh, so one parked Astra was thirty-three draw calls and the street's
# traffic was the largest single family in the frame after the crowd. The
# frame is bound by draw submission -- about twenty microseconds a call
# through the OpenGL 3.3 path -- so those materials are the cost, not the
# hundred and fifty thousand triangles behind them.
#
# What it does. Every material is normalised to three images -- base colour,
# metallic-roughness, normal -- taking a 1x1 pixel of the material's own
# factors where it has no texture, since a constant colour *is* a
# one-pixel texture. Those go into one atlas per channel, a cell each, and
# every face's UVs are remapped into its material's cell. Then the primitives
# that now share the merged material are concatenated. A material's roughness
# and metalness survive exactly, in the green and blue of the merged
# metallic-roughness map, which is the whole reason the merge does not flatten
# a car: chrome stays chrome and rubber stays rubber.
#
# What it deliberately does not merge:
#
#   * anything blended. Glass sorts, and sorting is per draw; three panes of
#     glass on one car are three draws whatever their materials say.
#   * a material whose UVs leave the unit square. An atlas cell cannot tile,
#     and a remap that assumed it could would smear a body panel across the
#     interior.
#   * a material whose own texture is larger than the cell. The two cars the
#     closest viewpoints park three metres from carry 2k paint on purpose;
#     putting that through a 512-pixel cell to save one draw call is the
#     trade this project does not make.
#
# The file it writes is a complete glTF rebuilt from the one it read -- same
# nodes, same names, same transforms, same winding, same alpha modes -- so
# `wheel_fl` is still `wheel_fl` and everything downstream is unchanged.
import argparse
import base64
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DERIVED = os.path.join(ROOT, "assets", "external", "downloads", "derived", "vehicles")

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
DTYPES = {5120: "i1", 5121: "u1", 5122: "i2", 5123: "u2", 5125: "u4", 5126: "f4"}
# The largest cell a merged material may occupy, and the largest atlas. A
# material whose own texture is bigger than the cell is downscaled into it,
# which is the trade for every interior panel and every piece of trim and the
# wrong trade for the paint on the flank of the car -- so the paint is kept
# apart by area share instead of by size (see `consolidate`). 512 is a
# texture-memory decision as much as a picture one: the atlas holds three
# channels, so a cell twice this size is four times the memory on every car
# in the street for detail nobody resolves through a side window.
CELL = 512
MAX_ATLAS = 2048
# The far level of detail is seen past forty-five metres, where a car is a
# hundred pixels long.
FAR_CELL = 128
FAR_ATLAS = 1024
# A textured material covering more of the car than this keeps its own draw.
# On every model in this set that is the body, and on some of them the tyre.
BIG_SHARE = 0.22


def log(*args):
    print("atlas:", *args, flush=True)


def read_glb(path):
    with open(path, "rb") as handle:
        blob = handle.read()
    magic, _v, _l = struct.unpack_from("<III", blob, 0)
    if magic != 0x46546C67:
        raise ValueError(f"{path}: not a glb")
    offset, document, binary = 12, None, b""
    while offset + 8 <= len(blob):
        size, kind = struct.unpack_from("<II", blob, offset)
        chunk = blob[offset + 8: offset + 8 + size]
        if kind == 0x4E4F534A:
            document = json.loads(chunk.decode("utf-8"))
        elif kind == 0x004E4942:
            binary = chunk
        offset += 8 + size + (-size % 4)
    return document, binary


def accessor(document, binary, index):
    acc = document["accessors"][index]
    count = acc["count"]
    width = COMPONENTS[acc["type"]]
    dtype = np.dtype(DTYPES[acc["componentType"]]).newbyteorder("<")
    if "bufferView" not in acc:
        return np.zeros((count, width), dtype=dtype)
    view = document["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or width * dtype.itemsize
    raw = np.frombuffer(binary, dtype="u1",
                        count=stride * (count - 1) + width * dtype.itemsize, offset=start)
    rows = np.lib.stride_tricks.as_strided(
        raw, shape=(count, width * dtype.itemsize), strides=(stride, 1)).copy()
    return rows.view(dtype).reshape(count, width)


def image_bytes(document, binary, index):
    image = document["images"][index]
    if "bufferView" in image:
        view = document["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        return binary[start:start + view["byteLength"]]
    uri = image.get("uri", "")
    if uri.startswith("data:"):
        return base64.b64decode(uri.split(",", 1)[1])
    raise ValueError("an image with neither a buffer view nor a data URI")


def texture_image(document, binary, texture_info):
    """The PIL image behind a textureInfo, or None."""
    if texture_info is None:
        return None
    texture = document["textures"][texture_info["index"]]
    source = texture.get("source")
    if source is None:
        return None
    return Image.open(io.BytesIO(image_bytes(document, binary, source)))


def srgb_pixel(colour):
    """A 1x1 base-colour image from a linear baseColorFactor."""
    def encode(value):
        value = max(0.0, min(1.0, float(value)))
        return value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    rgba = tuple(int(round(encode(c) * 255.0)) for c in colour[:3]) + (
        int(round(max(0.0, min(1.0, colour[3] if len(colour) > 3 else 1.0)) * 255.0)),)
    return Image.new("RGBA", (1, 1), rgba)


def material_sources(document, binary, material, flat=False):
    """Base colour, metallic-roughness and normal for one material, as images.

    A material without a texture becomes a one-pixel image of its own factors,
    which is the whole trick: after this every material looks the same to the
    atlas and the merge does not care which of them the author bothered to
    paint. @p flat forces that reading even where the material points at a
    map, for the materials whose only maps are tiled a hundred times over.
    """
    pbr = material.get("pbrMetallicRoughness", {})
    base = None if flat else texture_image(document, binary, pbr.get("baseColorTexture"))
    factor = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    if base is None:
        base = srgb_pixel(factor)
        base_factor = [1.0, 1.0, 1.0, factor[3] if len(factor) > 3 else 1.0]
    else:
        base = base.convert("RGBA")
        base_factor = list(factor)

    metal = float(pbr.get("metallicFactor", 1.0))
    rough = float(pbr.get("roughnessFactor", 1.0))
    mr = None if flat else texture_image(document, binary,
                                         pbr.get("metallicRoughnessTexture"))
    if mr is None:
        mr = Image.new("RGB", (1, 1),
                       (255, int(round(rough * 255.0)), int(round(metal * 255.0))))
    else:
        mr = mr.convert("RGB")
        # The factors multiply the map; fold them in so the merged material
        # can carry 1.0 for both.
        pixels = np.asarray(mr, dtype=np.float32)
        pixels[..., 1] *= rough
        pixels[..., 2] *= metal
        mr = Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8), "RGB")

    normal = None if flat else texture_image(document, binary, material.get("normalTexture"))
    if normal is None:
        normal = Image.new("RGB", (1, 1), (128, 128, 255))
    else:
        normal = normal.convert("RGB")
    return base, mr, normal, base_factor


def paste_cell(atlas, image, x, y, size, pad):
    """Scales @p image into a cell and replicates its edge into the padding.

    The padding is what keeps one cell out of the next one's mip levels: the
    content build compiles every model image with a chain, and a cell whose
    border is its neighbour's colour bleeds that colour across the part at
    distance.
    """
    inner = size - 2 * pad
    scaled = image.resize((inner, inner), Image.LANCZOS)
    block = Image.new(image.mode, (size, size))
    block.paste(scaled, (pad, pad))
    # Edges and corners, replicated.
    left = scaled.crop((0, 0, 1, inner)).resize((pad, inner), Image.NEAREST)
    right = scaled.crop((inner - 1, 0, inner, inner)).resize((pad, inner), Image.NEAREST)
    block.paste(left, (0, pad))
    block.paste(right, (size - pad, pad))
    top = block.crop((0, pad, size, pad + 1)).resize((size, pad), Image.NEAREST)
    bottom = block.crop((0, size - pad - 1, size, size - pad)).resize((size, pad), Image.NEAREST)
    block.paste(top, (0, 0))
    block.paste(bottom, (0, size - pad))
    atlas.paste(block, (x, y))


class Builder:
    """Collects the buffer of the file being written."""

    def __init__(self):
        self.blob = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data, target=None):
        while len(self.blob) % 4:
            self.blob.append(0)
        offset = len(self.blob)
        self.blob.extend(data)
        entry = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            entry["target"] = target
        self.views.append(entry)
        return len(self.views) - 1

    def attribute(self, array, kind, component, target=34962, minmax=False):
        array = np.ascontiguousarray(array)
        view = self.view(array.tobytes(), target)
        entry = {"bufferView": view, "componentType": component, "count": int(array.shape[0]),
                 "type": kind}
        if minmax:
            entry["min"] = [float(v) for v in array.min(axis=0)]
            entry["max"] = [float(v) for v in array.max(axis=0)]
        self.accessors.append(entry)
        return len(self.accessors) - 1


def surface_area(document, binary, primitive):
    attributes = primitive.get("attributes", {})
    if "POSITION" not in attributes or "indices" not in primitive:
        return 0.0
    points = accessor(document, binary, attributes["POSITION"]).astype("f8")
    tri = accessor(document, binary, primitive["indices"]).reshape(-1, 3)
    a, b, c = points[tri[:, 0]], points[tri[:, 1]], points[tri[:, 2]]
    return float(0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1).sum())


GENERATOR = "cna-street scripts/vehicle-atlas.py"


def consolidate(path, out_path, report=False, again=False):
    document, binary = read_glb(path)
    if not again and document.get("asset", {}).get("generator") == GENERATOR:
        log(f"{os.path.basename(path)}: already consolidated")
        return False
    materials = document.get("materials", [])
    meshes = document.get("meshes", [])
    far = os.path.basename(path).endswith("-far.glb")
    cell_cap = FAR_CELL if far else CELL
    atlas_cap = FAR_ATLAS if far else MAX_ATLAS

    # --- what each primitive is ------------------------------------------
    used = {}
    for mesh in meshes:
        for primitive in mesh.get("primitives", []):
            used.setdefault(primitive.get("material"), []).append(primitive)
    area = {index: sum(surface_area(document, binary, p) for p in group)
            for index, group in used.items()}
    total = max(sum(area.values()), 1e-9)

    # --- which materials may merge ---------------------------------------
    mergeable = []
    reasons = {}
    for index, material in enumerate(materials):
        if index not in used:
            continue
        name = material.get("name", str(index))
        if material.get("alphaMode", "OPAQUE") != "OPAQUE":
            reasons[name] = "blended"
            continue
        pbr = material.get("pbrMetallicRoughness", {})
        base = texture_image(document, binary, pbr.get("baseColorTexture"))
        # "Painted" means it has a base colour *map*. A material whose colour
        # is one factor is flat whatever else it carries: the Astra's twelve
        # trim materials each point at a shared 1k occlusion map through UVs
        # that run from -308 to +160, which tiles it three hundred times
        # across a wing mirror and resolves to one grey. Merging those and
        # keeping only their factors is exact to what they draw.
        if base is not None:
            # A painted material carries its own UV layout, and an atlas cell
            # cannot tile: a remap that assumed it could would smear a body
            # panel across the interior. An *unpainted* one has nothing to
            # smear -- its whole surface is one colour -- so its UVs are
            # simply rewritten to the middle of its cell.
            outside = False
            for primitive in used[index]:
                if "TEXCOORD_0" not in primitive.get("attributes", {}):
                    continue
                uv = accessor(document, binary,
                              primitive["attributes"]["TEXCOORD_0"]).astype("f8")
                if uv.size and (uv.min() < -0.002 or uv.max() > 1.002):
                    outside = True
            if outside:
                reasons[name] = "tiling UVs"
                continue
            share = area.get(index, 0.0) / total
            if share > BIG_SHARE:
                reasons[name] = f"{share * 100:.0f}% of the car"
                continue
            # How far a texture may be downscaled to join the atlas. Two
            # halvings near, four far -- a car past forty-five metres is a
            # hundred pixels long, and a part of it is ten.
            if base is not None and max(base.size) > (4 if far else 2) * cell_cap:
                reasons[name] = f"{base.size[0]}x{base.size[1]} texture"
                continue
        mergeable.append(index)

    if report:
        log(f"{os.path.basename(path)}: {len(materials)} materials, "
            f"{sum(len(v) for v in used.values())} primitives; "
            f"{len(mergeable)} mergeable")
        for name, why in reasons.items():
            log(f"    kept apart: {name} ({why})")

    # --- the atlas --------------------------------------------------------
    # Even with nothing to merge the file is rebuilt: an exported car carries
    # several primitives of the *same* material (the Logan has fourteen over
    # four), and concatenating those is a draw call each on its own.
    columns = 1
    while columns * columns < max(len(mergeable), 1):
        columns += 1
    # The cell is as big as the biggest thing going into it and no bigger: a
    # car whose merged materials are all constant colours gets a sixteen-pixel
    # atlas, not a two-thousand-pixel one full of flat squares.
    largest = 1
    for index in mergeable:
        base, mr, normal, _f = material_sources(document, binary, materials[index],
                                                flat=texture_image(
                                                    document, binary,
                                                    materials[index]
                                                    .get("pbrMetallicRoughness", {})
                                                    .get("baseColorTexture")) is None)
        largest = max(largest, base.size[0], base.size[1], mr.size[0], normal.size[0])
    cap = min(cell_cap, atlas_cap // columns)
    cell = 4
    while cell < largest and cell * 2 <= cap:
        cell *= 2
    pad = max(1, cell // 32)
    size = cell * columns
    base_atlas = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    mr_atlas = Image.new("RGB", (size, size), (255, 255, 0))
    normal_atlas = Image.new("RGB", (size, size), (128, 128, 255))
    cells = {}
    any_normal = False
    flat = {}
    for slot, index in enumerate(mergeable if len(mergeable) >= 2 else []):
        unpainted = texture_image(document, binary,
                                  materials[index].get("pbrMetallicRoughness", {})
                                  .get("baseColorTexture")) is None
        base, mr, normal, _factor = material_sources(document, binary, materials[index],
                                                     flat=unpainted)
        flat[index] = base.size == (1, 1) and mr.size == (1, 1) and normal.size == (1, 1)
        if normal.size != (1, 1):
            any_normal = True
        x = (slot % columns) * cell
        y = (slot // columns) * cell
        paste_cell(base_atlas, base, x, y, cell, pad)
        paste_cell(mr_atlas, mr, x, y, cell, pad)
        paste_cell(normal_atlas, normal, x, y, cell, pad)
        cells[index] = (x + pad, y + pad, cell - 2 * pad)

    # --- rebuild the document --------------------------------------------
    builder = Builder()
    name = os.path.splitext(os.path.basename(out_path))[0]

    out_images = []
    out_textures = []
    # Two samplers: the atlas clamps, because a cell that wrapped would show
    # its neighbour, and everything that kept its own texture repeats,
    # because several of these models lay a 1k map over a panel eight times.
    out_samplers = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33071},
                    {"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]

    def add_image(image, label):
        buffer = io.BytesIO()
        image.save(buffer, format="PNG", optimize=True)
        view = builder.view(buffer.getvalue())
        out_images.append({"bufferView": view, "mimeType": "image/png",
                           "name": f"{name}_{label}"})
        out_textures.append({"sampler": 0, "source": len(out_images) - 1})
        return len(out_textures) - 1

    passed = {}

    def pass_through(texture_info, label):
        """A texture a material kept: the author's own bytes, not a re-encode.

        Re-encoding every JPEG as a PNG on the way through added four
        megabytes to a car for no picture at all.
        """
        source = document["textures"][texture_info["index"]].get("source")
        if source is None:
            return None
        if source in passed:
            return passed[source]
        raw = image_bytes(document, binary, source)
        mime = document["images"][source].get("mimeType")
        if mime is None:
            mime = "image/png" if raw[:4] == b"\x89PNG" else "image/jpeg"
        view = builder.view(raw)
        out_images.append({"bufferView": view, "mimeType": mime, "name": f"{name}_{label}"})
        out_textures.append({"sampler": 1, "source": len(out_images) - 1})
        passed[source] = len(out_textures) - 1
        return passed[source]

    out_materials = []
    remap = {}
    if cells:
        merged_base = add_image(base_atlas.crop((0, 0, size, size)), "atlas_base")
        merged_mr = add_image(mr_atlas, "atlas_mr")
        merged_normal = add_image(normal_atlas, "atlas_normal") if any_normal else None
        merged_material = {
            "name": "merged",
            "alphaMode": "OPAQUE",
            "doubleSided": any(materials[i].get("doubleSided", False) for i in cells),
            "pbrMetallicRoughness": {
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "baseColorTexture": {"index": merged_base},
                "metallicFactor": 1.0,
                "roughnessFactor": 1.0,
                "metallicRoughnessTexture": {"index": merged_mr},
            },
        }
        if merged_normal is not None:
            merged_material["normalTexture"] = {"index": merged_normal}
        out_materials.append(merged_material)
        remap = {index: 0 for index in cells}

    # Every material that stayed apart keeps its own images, re-added so the
    # written file carries only what it uses.
    for index, material in enumerate(materials):
        if index in cells or index not in used:
            continue
        copy = json.loads(json.dumps(material))
        pbr = copy.get("pbrMetallicRoughness", {})
        for slot, holder in (("baseColorTexture", pbr), ("metallicRoughnessTexture", pbr),
                             ("normalTexture", copy), ("occlusionTexture", copy),
                             ("emissiveTexture", copy)):
            info = holder.get(slot)
            if info is None:
                continue
            target = pass_through(info, f"m{index}_{slot}")
            if target is None:
                holder.pop(slot)
                continue
            info["index"] = target
            info.pop("texCoord", None)
        out_materials.append(copy)
        remap[index] = len(out_materials) - 1

    # --- the meshes -------------------------------------------------------
    out_meshes = []
    merged_primitives = 0
    kept_primitives = 0
    for mesh in meshes:
        groups = {}
        order = []
        for primitive in mesh.get("primitives", []):
            source = primitive.get("material")
            target = remap.get(source)
            if target is None:
                continue
            attributes = primitive.get("attributes", {})
            if "POSITION" not in attributes:
                continue
            position = accessor(document, binary, attributes["POSITION"]).astype("f4")
            count = position.shape[0]
            normal = (accessor(document, binary, attributes["NORMAL"]).astype("f4")
                      if "NORMAL" in attributes else np.zeros((count, 3), "f4"))
            tangent = (accessor(document, binary, attributes["TANGENT"]).astype("f4")
                       if "TANGENT" in attributes else
                       np.tile(np.array([1, 0, 0, 1], "f4"), (count, 1)))
            uv = (accessor(document, binary, attributes["TEXCOORD_0"]).astype("f4")
                  if "TEXCOORD_0" in attributes else np.zeros((count, 2), "f4"))
            indices = (accessor(document, binary, primitive["indices"]).reshape(-1).astype("u4")
                       if "indices" in primitive else np.arange(count, dtype="u4"))
            if source in cells:
                x, y, inner = cells[source]
                if flat.get(source, False):
                    # An unpainted material: its cell is one colour, so put
                    # every vertex in the middle of it and forget whatever UV
                    # layout the author had.
                    uv = np.tile(np.array([(x + inner * 0.5) / size,
                                           (y + inner * 0.5) / size], "f4"), (count, 1))
                else:
                    uv = np.clip(uv, 0.0, 1.0)
                    uv = np.stack([(x + uv[:, 0] * inner) / size,
                                   (y + uv[:, 1] * inner) / size], axis=1).astype("f4")
            slot = groups.get(target)
            if slot is None:
                groups[target] = [position, normal, tangent, uv, indices]
                order.append(target)
            else:
                slot[4] = np.concatenate([slot[4], indices + slot[0].shape[0]])
                slot[0] = np.concatenate([slot[0], position])
                slot[1] = np.concatenate([slot[1], normal])
                slot[2] = np.concatenate([slot[2], tangent])
                slot[3] = np.concatenate([slot[3], uv])
                merged_primitives += 1
        primitives = []
        for target in order:
            position, normal, tangent, uv, indices = groups[target]
            kept_primitives += 1
            primitives.append({
                "attributes": {
                    "POSITION": builder.attribute(position, "VEC3", 5126, minmax=True),
                    "NORMAL": builder.attribute(normal, "VEC3", 5126),
                    "TANGENT": builder.attribute(tangent, "VEC4", 5126),
                    "TEXCOORD_0": builder.attribute(uv, "VEC2", 5126),
                },
                "indices": builder.attribute(indices.reshape(-1, 1), "SCALAR", 5125,
                                             target=34963),
                "material": target,
                "mode": 4,
            })
        out_meshes.append({"name": mesh.get("name", "mesh"), "primitives": primitives})

    out = {
        "asset": {"version": "2.0",
                  "generator": GENERATOR},
        "scene": document.get("scene", 0),
        "scenes": document.get("scenes", [{"nodes": [0]}]),
        "nodes": document.get("nodes", []),
        "meshes": out_meshes,
        "materials": out_materials,
        "textures": out_textures,
        "images": out_images,
        "samplers": out_samplers,
        "accessors": builder.accessors,
        "bufferViews": builder.views,
        "buffers": [{"byteLength": len(builder.blob)}],
    }
    # Nodes may name cameras or skins the rebuild drops; nothing here has any.
    for node in out["nodes"]:
        node.pop("camera", None)
        node.pop("skin", None)

    text = json.dumps(out, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    blob = bytes(builder.blob)
    blob += b"\0" * (-len(blob) % 4)
    with open(out_path, "wb") as handle:
        handle.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        handle.write(struct.pack("<II", len(text), 0x4E4F534A))
        handle.write(text)
        handle.write(struct.pack("<II", len(blob), 0x004E4942))
        handle.write(blob)

    before = sum(len(v) for v in used.values())
    log(f"{os.path.basename(out_path)}: {len(materials)} materials / {before} primitives "
        f"-> {len(out_materials)} / {kept_primitives}"
        + (f", atlas {size}x{size} ({len(cells)} cells)" if cells else ", no atlas")
        + f", {os.path.getsize(out_path) // 1000} kB")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--in", dest="source", default=DERIVED)
    parser.add_argument("--out", dest="out", default=DERIVED)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--again", action="store_true",
                        help="consolidate a file this script already wrote")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    if not os.path.isdir(args.source):
        log("no derived vehicles: nothing to consolidate")
        return 0
    names = args.names or sorted(
        os.path.splitext(f)[0] for f in os.listdir(args.source) if f.endswith(".glb"))
    os.makedirs(args.out, exist_ok=True)
    done = 0
    for name in names:
        source = os.path.join(args.source, name + ".glb")
        if not os.path.isfile(source):
            log(name, "not derived")
            continue
        if consolidate(source, os.path.join(args.out, name + ".glb"), args.report, args.again):
            done += 1
    log(f"{done} of {len(names)} consolidated")
    return 0


sys.exit(main())

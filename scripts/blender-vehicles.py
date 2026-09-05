# Turns a downloaded Sketchfab/Objaverse car into the two levels of detail the
# street parks. Runs inside Blender:
#
#     blender -b --python-use-system-env --python scripts/blender-vehicles.py -- \
#         --downloads assets/external/downloads --out assets/external/downloads/derived/vehicles \
#         [--preview <dir>] [--max-texture 1024] [name ...]
#
# What each model gets, in order:
#
#   * imported with Blender's glTF importer, which is the one place a
#     specular-glossiness material (the Punto) becomes metallic-roughness, the
#     only form CNA's importer takes;
#   * the backdrop, the baked shadow plane and any other object the table
#     below names dropped, and every object's transform applied;
#   * turned so the long axis is Y and the front is -Y -- Blender's front, which
#     the glTF exporter writes as +Z, the way this project's vehicles face;
#   * scaled uniformly to the real car's length, stood on z = 0 and centred,
#     because the eight files arrive in five different units;
#   * a body whose windows are only in its texture's alpha (the Kadett) split
#     into an opaque body and a blended glass part, since a blended body casts
#     no shadow and sorts as a pane;
#   * the wheels found and split off: every loose piece of geometry that is a
#     cylinder of tyre size standing on the ground is a tyre, the four wheel
#     centres come from those, and every loose piece that lies inside a
#     wheel's cylinder -- rim, hub cap, brake disc -- goes with it. Each wheel
#     is exported as its own node, `wheel_fl` .. `wheel_rr`, with its mesh
#     centred on the axle and the node's translation saying where the axle
#     is, which is what lets the scene roll and steer an authored car. A model
#     whose wheels are not separable (no four tyres found) is exported as
#     before and stays parked;
#   * everything else joined into one `body` mesh with one primitive per
#     material, so a car costs as many draws as it has materials rather than
#     as many as it had objects;
#   * textures capped at --max-texture, or at the car's own `texture` where
#     the table raises it: the content build now compiles every model image
#     with a mip chain (the workaround for docs/cna-findings.md GLTF-206), so
#     the cap is texture memory rather than shimmer;
#   * decimated to the near and far budgets and exported twice as GLB;
#   * the GLB's JSON patched: glass materials BLEND with a sensible alpha,
#     everything else OPAQUE, and every KHR_materials_* extension stripped,
#     since CNA declines files that carry ones it does not implement.
#
# The table is the record of what was decided per car: which objects are
# scenery rather than car, how long the real one is, and how much geometry
# the two levels may keep.
import argparse
import json
import math
import os
import re
import struct
import sys

import bmesh
import bpy
import mathutils
import numpy as np

VEHICLES = {
    # name: uid on Sketchfab and in Objaverse, the real car's length in metres,
    # objects that are backdrop rather than car, materials whose texture alpha
    # holds the glazing, and the near/far triangle budgets.
    # `texture` raises the cap for the two cars the closest viewpoints park
    # three metres from: with the content build compiling every model image
    # with a mip chain, a 2k paint no longer shimmers at twenty metres.
    # `tyres="hint"`: the Astra's mesh is a soup whose tyre pieces come out
    # partial, so its tyres are taken from the objects that stand on the
    # ground rather than from connected pieces.
    "opel-astra-gtc":    dict(uid="d76ad5a0495443eda24a9052565ef9a8", length=4.466,
                              drop=["Object_76"], near=150000, far=18000, texture=2048,
                              tyres="hint"),
    "fiat-punto-gt":     dict(uid="48db6facb4b64e99b60f36b8c01185e1", length=3.760,
                              drop=["Punto_GT_8"], near=60000, far=14000),
    "renault-logan":     dict(uid="e4f9463f6e004b90bb977d12f6375b9c", length=4.250,
                              drop=[], near=60000, far=14000),
    "mercedes-sprinter": dict(uid="f69de1315bb049c8946d57f6006acd73", length=5.910,
                              drop=[], near=60000, far=14000),
    "vaz-2104":          dict(uid="ba563a31b4754a66a5f0e7f893145922", length=4.120,
                              drop=[], near=60000, far=14000),
    "small-price-car":   dict(uid="67c84e4d30ae42fda22c0a0c7526df26", length=4.260,
                              drop=[], alpha_split=["Material"], near=60000, far=14000),
    "honda-civic-ek":    dict(uid="b9604dd71c6546ec842702682019492a", length=4.180,
                              drop=[], near=120000, far=18000, texture=2048),
    # The Mini arrives facing the other way from the seven others, which
    # nobody saw while it was parked and everybody saw once it drove.
    "mini-cooper-s":     dict(uid="b262744901a04cda823f17289d1a4847", length=3.655,
                              drop=[], near=60000, far=14000, flip=True),
}

GLASS_WORDS = ("glass", "window", "windsh", "vitre", "glas", "glazing")
LAMP_WORDS = ("red_glass", "lights_glass", "light_glass", "phare", "fara", "lamp")


def log(*args):
    print("vehicles:", *args, flush=True)


def is_glass(name):
    n = name.lower()
    return any(w in n for w in GLASS_WORDS) and not any(w in n for w in LAMP_WORDS)


def mesh_objects():
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def select_only(objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    if objects:
        bpy.context.view_layer.objects.active = objects[0]


def bounds(objects):
    lo = mathutils.Vector((1e30, 1e30, 1e30))
    hi = mathutils.Vector((-1e30, -1e30, -1e30))
    for o in objects:
        for c in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(c)
            lo = mathutils.Vector(map(min, lo, w))
            hi = mathutils.Vector(map(max, hi, w))
    return lo, hi


def triangle_count(objects):
    return sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objects)


def split_alpha(obj, material_name):
    """Moves the faces whose texture alpha is under a half onto a glass copy of
    the material. For a body whose windows live only in the alpha channel."""
    slot_index = next((i for i, m in enumerate(obj.data.materials) if m and m.name == material_name), None)
    if slot_index is None:
        return
    material = obj.data.materials[slot_index]
    image = None
    for node in material.node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image is not None:
            image = node.image
            break
    if image is None:
        log("  alpha split: no image on", material_name)
        return
    w, h = image.size
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)
    alpha = pixels[:, :, 3]
    uv = obj.data.uv_layers.active.data
    glass = material.copy()
    glass.name = material_name + "_glass"
    obj.data.materials.append(glass)
    glass_index = len(obj.data.materials) - 1
    moved = 0
    for poly in obj.data.polygons:
        if poly.material_index != slot_index:
            continue
        u = np.mean([uv[l].uv.x for l in poly.loop_indices]) % 1.0
        v = np.mean([uv[l].uv.y for l in poly.loop_indices]) % 1.0
        a = alpha[min(h - 1, int(v * h)), min(w - 1, int(u * w))]
        if a < 0.5:
            poly.material_index = glass_index
            moved += 1
    log(f"  alpha split: {moved} faces of '{material_name}' are glass")


TYRE_WORDS = ("tire", "tyre", "tread", "gum", "wheel", "rim")
TYRE_NOT = ("steer", "panel", "mask", "well", "arch", "cover")


def is_tyre_material(name):
    tokens = [t for t in re.split(r"[^a-z0-9]+", name.lower()) if t]
    if any(n in name.lower() for n in TYRE_NOT):
        return False
    return any(t.startswith(w) for t in tokens for w in TYRE_WORDS)


def face_table(mesh):
    """Per-face bounds and centroid, as numpy arrays, for every polygon."""
    n = len(mesh.vertices)
    co = np.empty(n * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    loops = np.empty(len(mesh.loops), dtype=np.int32)
    mesh.loops.foreach_get("vertex_index", loops)
    f = len(mesh.polygons)
    start = np.empty(f, dtype=np.int32)
    mesh.polygons.foreach_get("loop_start", start)
    material = np.empty(f, dtype=np.int32)
    mesh.polygons.foreach_get("material_index", material)
    points = co[loops]
    lo = np.minimum.reduceat(points, start, axis=0)
    hi = np.maximum.reduceat(points, start, axis=0)
    return lo, hi, (lo + hi) * 0.5, material


def face_islands(mesh):
    """Connected-component label per face, from shared vertices."""
    n = len(mesh.vertices)
    parent = np.arange(n, dtype=np.int64)

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    loops = np.empty(len(mesh.loops), dtype=np.int32)
    mesh.loops.foreach_get("vertex_index", loops)
    f = len(mesh.polygons)
    start = np.empty(f, dtype=np.int32)
    mesh.polygons.foreach_get("loop_start", start)
    total = np.empty(f, dtype=np.int32)
    mesh.polygons.foreach_get("loop_total", total)
    for s0, t in zip(start, total):
        root = find(loops[s0])
        for k in range(1, t):
            other = find(loops[s0 + k])
            if other != root:
                parent[other] = root
    return np.array([find(loops[s0]) for s0 in start])


def find_wheels(car, name, prefer_hint=False):
    """Finds the four wheels of a joined car and returns per-face wheel slots.

    A tyre is named for what it is in most of these files -- a material
    called tire, tyre, tread, gum (the Russian modellers' word) or wheel --
    and where it is not, it is the one connected piece of a car that is a
    cylinder of tyre size standing on the ground. Either way the tyre faces
    in each quarter of the car give the wheel: its centre from the faces on
    the ground, its radius from the highest tyre face over them, its width
    from their spread across the car. Every face that lies inside that
    cylinder with a little slack then turns with the wheel -- the rim, the
    hub cap, the brake disc -- except a caliper, which is bolted to the hub
    and named for it in the models that have one.

    Returns (slots, wheels): a per-face array of -1 for the body or 0..3 for
    a wheel, and the four wheels' centre, radius and slot name; or (None,
    None) when the model does not separate.
    """
    mesh = car.data
    materials = [m.name if m else "" for m in mesh.materials]
    lo, hi, centre, material = face_table(mesh)
    tyre_slots = np.array([i for i, m in enumerate(materials) if is_tyre_material(m)], dtype=np.int32)
    hint = np.zeros(len(material), dtype=np.int32)
    if "tyre_hint" in mesh.attributes:
        mesh.attributes["tyre_hint"].data.foreach_get("value", hint)
    if tyre_slots.size:
        tyre = np.isin(material, tyre_slots)
        how = "material " + ", ".join(materials[i] for i in tyre_slots)
    elif prefer_hint and hint.any():
        tyre = hint > 0
        how = "the tyre nodes' names"
    else:
        # Connected pieces of tyre size first, the node-name hint only for a
        # file whose mesh is a soup of loose triangles and has no pieces: a
        # hinted object can hold two tyres and stand on the ground with
        # the other pair hanging above it, and the pieces tell them apart.
        island = face_islands(mesh)
        tyre = np.zeros(len(material), dtype=bool)
        found = 0
        for label in np.unique(island):
            member = island == label
            size = hi[member].max(axis=0) - lo[member].min(axis=0)
            bottom = lo[member].min(axis=0)[2]
            if 0.45 <= size[2] <= 0.90 and size[0] <= 0.42 and abs(size[1] - size[2]) <= 0.15 * size[2] \
               and bottom <= 0.12:
                tyre |= member
                found += 1
        how = "connected pieces of tyre size"
        if found < 4 and hint.any():
            tyre = hint > 0
            how = "the tyre nodes' names"
    if tyre.sum() < 4 or lo[tyre, 2].min() > 0.15:
        log(f"{name}: no tyres on the ground ({how}); the wheels stay on the body")
        return None, None
    # Four quarters: left/right by x, front/back by the mid-point of the
    # tyre faces' y range -- of all of them, because in one file the front
    # tyres hang a hand above the ground the rear ones stand on.
    ty = centre[tyre, 1]
    mid_y = (ty.min() + ty.max()) * 0.5
    slots = np.full(len(material), -1, dtype=np.int32)
    wheels = []
    for index, (left, front) in enumerate(((True, True), (False, True), (True, False), (False, False))):
        quarter = tyre & ((centre[:, 0] > 0) == left) & ((centre[:, 1] < mid_y) == front)
        if not quarter.any():
            log(f"{name}: no tyre in one quarter ({how}); the wheels stay on the body")
            return None, None
        cx = centre[quarter, 0].mean()
        cy = (lo[quarter, 1].min() + hi[quarter, 1].max()) * 0.5
        near = tyre & (np.hypot(centre[:, 0] - cx, centre[:, 1] - cy) < 0.55)
        # The axle is the centre of the tyre's own box, not the ground and
        # not the contact patch: a model a few centimetres off level, or a
        # tread whose lowest faces sit to one side, put the axle off the
        # tyre's centre, and a wheel turned about the wrong point hops. Two
        # passes, the second keeping only what lies within the tyre's
        # radius of the first pass's axle.
        radius = cz = 0.0
        for _ in range(2):
            y0, y1 = lo[near, 1].min(), hi[near, 1].max()
            z0, z1 = lo[near, 2].min(), hi[near, 2].max()
            cy, cz = (y0 + y1) * 0.5, (z0 + z1) * 0.5
            radius = max(y1 - y0, z1 - z0) * 0.5
            near = tyre & (np.abs(centre[:, 0] - cx) < 0.35) \
                   & (np.hypot(centre[:, 1] - cy, centre[:, 2] - cz) <= radius + 0.04)
            if not near.any():
                break
        if near.any():
            cx = (lo[near, 0].min() + hi[near, 0].max()) * 0.5
            width = hi[near, 0].max() - lo[near, 0].min()
        else:
            width = 0.0
        if not (0.24 <= radius <= 0.48) or width > 0.45 or width <= 0.0:
            log(f"{name}: a tyre came out {radius:.2f} m in radius and {width:.2f} m wide ({how}); "
                "the wheels stay on the body")
            return None, None
        # Everything inside the cylinder, by its bounds, unless it is a caliper.
        inside = (np.abs(lo[:, 0] - cx) <= width * 0.5 + 0.10) & (np.abs(hi[:, 0] - cx) <= width * 0.5 + 0.10)
        for ys in (lo[:, 1], hi[:, 1]):
            for zs in (lo[:, 2], hi[:, 2]):
                inside &= np.hypot(ys - cy, zs - cz) <= radius + 0.03
        caliper = np.array([("calip" in m.lower()) for m in materials], dtype=bool)
        inside &= ~caliper[material]
        slots[inside] = index
        log(f"  {('f' if front else 'r') + ('l' if left else 'r')}: tyre faces {int(near.sum())}, "
            f"inside {int(inside.sum())}, radius {radius:.3f}, width {width:.2f}, "
            f"axle ({cx:+.2f}, {cy:+.2f}, {cz:.2f})")
        wheels.append(dict(centre=mathutils.Vector((float(cx), float(cy), float(cz))),
                           radius=float(radius), width=float(width),
                           slot=("f" if front else "r") + ("l" if left else "r"),
                           faces=int(inside.sum())))
    log(f"{name}: four wheels by {how}")
    return slots, wheels


def split_wheels(car, slots, wheels):
    """Separates each wheel's faces into an object of its own, centred on
    its axle with the axle as the object's location. The slot travels on a
    face attribute, because every separation renumbers the faces that stay."""
    attribute = car.data.attributes.new("wheel_slot", "INT", "FACE")
    attribute.data.foreach_set("value", slots.tolist())
    out = []
    for index, wheel in enumerate(wheels):
        select_only([car])
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_mode(type="FACE")
        bpy.ops.mesh.select_all(action="DESELECT")
        bpy.ops.object.mode_set(mode="OBJECT")
        current = np.empty(len(car.data.polygons), dtype=np.int32)
        car.data.attributes["wheel_slot"].data.foreach_get("value", current)
        chosen = current == index
        if not chosen.any():
            log(f"  wheel_{wheel['slot']}: no faces left to separate")
            return None
        car.data.polygons.foreach_set("select", chosen.tolist())
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.separate(type="SELECTED")
        bpy.ops.object.mode_set(mode="OBJECT")
        piece = [o for o in bpy.context.selected_objects if o != car][0]
        piece.name = "wheel_" + wheel["slot"]
        piece.data.name = piece.name
        piece.data.transform(mathutils.Matrix.Translation(-wheel["centre"]))
        piece.location = wheel["centre"]
        out.append(piece)
    for o in [car] + out:
        for name in ("wheel_slot", "tyre_hint"):
            if name in o.data.attributes:
                o.data.attributes.remove(o.data.attributes[name])
    return out


def join_into(objects, name):
    """Joins objects into one, named, with doubled vertices merged."""
    select_only(objects)
    if len(objects) > 1:
        bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = name
    # The mesh too: the exporter names the glTF mesh after the datablock,
    # and the scene picks a wheel out of the file by that name.
    joined.data.name = name
    bm = bmesh.new()
    bm.from_mesh(joined.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(joined.data)
    bm.free()
    # One UV set and no vertex colours. The renderer instances a prop by
    # binding its transforms in a second vertex stream, and CNA refuses a
    # mesh whose own stream already carries the usages that stream declares:
    # a second TEXCOORD or a COLOR attribute from Sketchfab's export is
    # exactly that, and the car then draws nothing.
    while len(joined.data.uv_layers) > 1:
        joined.data.uv_layers.remove(joined.data.uv_layers[-1])
    for attribute in list(joined.data.color_attributes):
        joined.data.color_attributes.remove(attribute)
    for material in joined.data.materials:
        if material is not None:
            material.use_backface_culling = not is_glass(material.name)
    return joined


def cap_textures(limit):
    for image in bpy.data.images:
        if image.size[0] > limit or image.size[1] > limit:
            s = limit / max(image.size)
            image.scale(max(1, int(image.size[0] * s)), max(1, int(image.size[1] * s)))


def decimate_to(objects, budget):
    total = triangle_count(objects)
    if total <= budget:
        return total
    ratio = budget / total
    for o in objects:
        select_only([o])
        mod = o.modifiers.new("decimate", "DECIMATE")
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return triangle_count(objects)


def export(objects, path):
    select_only(objects)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_texcoords=True,
                              export_normals=True, export_tangents=True, export_materials="EXPORT",
                              export_image_format="AUTO", export_cameras=False, export_lights=False,
                              export_animations=False, export_skins=False,
                              export_vertex_color="NONE", export_attributes=False)


def flip_winding(document, binary):
    """Swaps the second and third corner of every triangle, in place.

    glTF winds a front face counter-clockwise and CNA's importer keeps that
    order, but CNA's default cull (RasterizerState::CullCounterClockwise,
    docs/cna-findings.md CNA-F5) drops exactly those faces. Every model this
    project imported before the cars was double-sided, which is why nobody
    saw it: the first single-sided body drew inside out, its far flank
    through its near one. Reversing the index order is the whole fix -- the
    normals are untouched, so the lighting is the author's.
    """
    views = document.get("bufferViews", [])
    accessors = document.get("accessors", [])
    flipped = 0
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4 or "indices" not in primitive:
                continue
            accessor = accessors[primitive["indices"]]
            view = views[accessor["bufferView"]]
            offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            count = accessor["count"]
            fmt = {5121: "B", 5123: "H", 5125: "I"}[accessor["componentType"]]
            size = struct.calcsize(fmt)
            for tri in range(count // 3):
                a = offset + (tri * 3 + 1) * size
                b = offset + (tri * 3 + 2) * size
                # Copies, not views: two slices of one memoryview alias the
                # same bytes, and swapping them in place writes the second
                # index over the third and leaves every triangle degenerate.
                second, third = bytes(binary[a:a + size]), bytes(binary[b:b + size])
                binary[a:a + size], binary[b:b + size] = third, second
            flipped += count // 3
    return flipped


def patch_glb(path):
    """Alpha modes by material name, CNA's winding, and no material extensions."""
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    json_length, json_type = struct.unpack_from("<II", data, 12)
    document = json.loads(data[20:20 + json_length])
    rest = bytearray(data[20 + json_length:])
    # The binary chunk follows its own eight-byte header.
    binary_length, binary_type = struct.unpack_from("<II", rest, 0)
    assert binary_type == 0x004E4942, "second chunk is not BIN"
    binary = memoryview(rest)[8:8 + binary_length]
    flip_winding(document, binary)
    for material in document.get("materials", []):
        name = material.get("name", "")
        material.pop("extensions", None)
        pbr = material.setdefault("pbrMetallicRoughness", {})
        if is_glass(name):
            material["alphaMode"] = "BLEND"
            material["doubleSided"] = True
            factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
            if "baseColorTexture" not in pbr:
                # Tinted automotive glass: dark enough that what shows
                # through it is the street and the reflection, not a milky
                # pane -- a pale factor at half alpha turned every window of
                # the Logan and the Punto into frosted glass -- and not black,
                # because a pane that reflects nothing of its own reads as a
                # hole.
                factor[3] = min(0.62, max(0.45, factor[3]))
                factor[0:3] = [min(max(c, 0.04), 0.10) for c in factor[0:3]]
            pbr["baseColorFactor"] = factor
            pbr["metallicFactor"] = 0.0
            pbr["roughnessFactor"] = min(pbr.get("roughnessFactor", 0.1), 0.18)
        elif material.get("alphaMode") == "BLEND" and any(w in name.lower() for w in LAMP_WORDS):
            factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
            factor[3] = max(0.55, factor[3])
            pbr["baseColorFactor"] = factor
        else:
            material["alphaMode"] = "OPAQUE"
            material.pop("alphaCutoff", None)
    for key in ("extensionsUsed", "extensionsRequired"):
        if key in document:
            document[key] = [e for e in document[key] if not e.startswith("KHR_materials_")]
            if not document[key]:
                del document[key]
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((4 - len(encoded) % 4) % 4)
    out = bytearray()
    out += struct.pack("<III", magic, version, 12 + 8 + len(encoded) + len(rest))
    out += struct.pack("<II", len(encoded), json_type)
    out += encoded
    out += rest
    open(path, "wb").write(out)
    return document


def preview(objects, path):
    lo, hi = bounds(objects)
    size = hi - lo
    centre = (lo + hi) * 0.5
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.render.resolution_x = 800
    scene.render.resolution_y = 450
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.clip_end = 1000.0
    cam_data.ortho_scale = max(size) * 1.2
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    d = mathutils.Vector((1.0, -1.0, 0.7)).normalized()
    cam.location = centre + d * max(size) * 3
    cam.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)
    bpy.data.cameras.remove(cam_data)


def process(name, spec, args):
    source = os.path.join(args.downloads, "objaverse", spec["uid"] + ".glb")
    if not os.path.isfile(source):
        log(name, "not fetched:", source)
        return False
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=source)

    # Which objects are tyres by name -- theirs or their parent node's, since
    # Sketchfab hangs each mesh under an empty that carries the author's
    # name -- written onto their faces so the hint survives the join. The
    # objects that stand on the ground join them once the model is scaled.
    objects = mesh_objects()
    for o in objects:
        hinted = is_tyre_material(o.name) or (o.parent is not None and is_tyre_material(o.parent.name))
        attribute = o.data.attributes.new("tyre_hint", "INT", "FACE")
        attribute.data.foreach_set("value", [1 if hinted else 0] * len(o.data.polygons))

    # Flatten the hierarchy so every transform is in the mesh.
    select_only([o for o in bpy.context.scene.objects])
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    select_only(objects)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH" or o.name in spec.get("drop", []):
            bpy.data.objects.remove(o, do_unlink=True)
    objects = mesh_objects()

    # The long axis along Y, the front toward -Y.
    lo, hi = bounds(objects)
    size = hi - lo
    if size.x > size.y:
        for o in objects:
            o.rotation_euler.z += math.pi / 2
    if spec.get("flip", False):
        for o in objects:
            o.rotation_euler.z += math.pi
    select_only(objects)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    lo, hi = bounds(objects)
    size = hi - lo
    scale = spec["length"] / size.y
    centre = (lo + hi) * 0.5
    for o in objects:
        o.location = (o.location - mathutils.Vector((centre.x, centre.y, lo.z))) * scale
        o.scale = (scale, scale, scale)
    select_only(objects)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    lo, hi = bounds(objects)
    log(f"{name}: {len(objects)} objects, {triangle_count(objects)} triangles, "
        f"{(hi - lo).x:.2f} x {(hi - lo).y:.2f} x {(hi - lo).z:.2f} m after scaling by {scale:.4f}")
    # And by where they stand: with the backdrop dropped, the only objects
    # that touch the ground and are under a metre tall are the tyres -- in
    # one file all four are one object named for nothing.
    for o in objects:
        olo, ohi = bounds([o])
        if olo.z <= 0.02 and (ohi.z - olo.z) <= 0.90 and "tyre_hint" in o.data.attributes:
            o.data.attributes["tyre_hint"].data.foreach_set("value", [1] * len(o.data.polygons))

    for o in objects:
        for material_name in spec.get("alpha_split", []):
            split_alpha(o, material_name)

    cap_textures(max(args.max_texture, spec.get("texture", 0)))

    # One object, one primitive per material, then the wheels split off it
    # where the model lets them be found: the body and four wheels, or the
    # body alone.
    car = join_into(objects, "body")
    objects = [car]
    slots, wheels = find_wheels(car, name, spec.get("tyres") == "hint")
    wheel_objects = split_wheels(car, slots, wheels) if wheels is not None else None
    if wheel_objects is None:
        if "tyre_hint" in car.data.attributes:
            car.data.attributes.remove(car.data.attributes["tyre_hint"])
    else:
        for wheel, wheel_object in zip(wheels, wheel_objects):
            log(f"  wheel_{wheel['slot']}: {triangle_count([wheel_object])} triangles, "
                f"radius {wheel['radius']:.3f} m, {wheel['width']:.2f} m wide, at "
                f"({wheel['centre'].x:+.2f}, {wheel['centre'].z:.2f}, {-wheel['centre'].y:+.2f})")
        objects = [car] + wheel_objects

    os.makedirs(args.out, exist_ok=True)
    near = decimate_to(objects, spec["near"])
    near_path = os.path.join(args.out, name + ".glb")
    export(objects, near_path)
    document = patch_glb(near_path)
    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        preview(objects, os.path.join(args.preview, name + ".png"))
    # The far copy carries its wheels welded on at the straight-ahead
    # position, as the lofts' far copy does: past forty-five metres a wheel
    # is eight pixels across and its rotation is invisible.
    if len(objects) > 1:
        for wheel_object in objects[1:]:
            wheel_object.data.transform(mathutils.Matrix.Translation(wheel_object.location))
            wheel_object.location = (0.0, 0.0, 0.0)
        objects = [join_into(objects, name)]
    far = decimate_to(objects, spec["far"])
    # The far copy is seen past forty-five metres, where a car is a hundred
    # pixels long and a 1k texture is a 1k texture for nothing.
    cap_textures(min(args.max_texture, 512))
    far_path = os.path.join(args.out, name + "-far.glb")
    export(objects, far_path)
    patch_glb(far_path)
    materials = ", ".join(f"{m.get('name')}:{m.get('alphaMode', 'OPAQUE')[0]}"
                          for m in document.get("materials", []))
    log(f"{name}: near {near} triangles -> {os.path.getsize(near_path) // 1000} kB, "
        f"far {far} triangles -> {os.path.getsize(far_path) // 1000} kB; materials {materials}")
    return True


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--downloads", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--preview", default="")
    parser.add_argument("--max-texture", type=int, default=1024)
    parser.add_argument("names", nargs="*")
    args = parser.parse_args(argv)
    names = args.names or list(VEHICLES.keys())
    done = 0
    for name in names:
        if name not in VEHICLES:
            log("unknown vehicle", name)
            continue
        if process(name, VEHICLES[name], args):
            done += 1
    log(f"{done} of {len(names)} vehicle(s) written to {args.out}")


main()

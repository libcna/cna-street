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
#   * every object joined into one mesh with one primitive per material, so a
#     car costs as many draws as it has materials rather than as many as it
#     had objects;
#   * textures capped at --max-texture, because an imported image reaches the
#     renderer with one mip level (docs/cna-findings.md GLTF-206) and a 4k map
#     on a car seen at ten metres shimmers;
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
    "opel-astra-gtc":    dict(uid="d76ad5a0495443eda24a9052565ef9a8", length=4.466,
                              drop=["Object_76"], near=150000, far=18000),
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
                              drop=[], near=120000, far=18000),
    "mini-cooper-s":     dict(uid="b262744901a04cda823f17289d1a4847", length=3.655,
                              drop=[], near=60000, far=14000),
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
                factor[3] = min(0.62, max(0.34, factor[3]))
                # Tinted, not black: a pane that reflects nothing of its own
                # colour reads as a hole.
                factor[0:3] = [max(c, 0.06) for c in factor[0:3]]
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

    # Flatten the hierarchy so every transform is in the mesh.
    objects = mesh_objects()
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

    for o in objects:
        for material_name in spec.get("alpha_split", []):
            split_alpha(o, material_name)

    cap_textures(args.max_texture)

    # One object, one primitive per material.
    select_only(objects)
    bpy.ops.object.join()
    car = bpy.context.view_layer.objects.active
    car.name = name
    # Merge the doubled vertices Sketchfab's export leaves along every seam,
    # which halves the vertex count before the decimator sees it.
    bm = bmesh.new()
    bm.from_mesh(car.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(car.data)
    bm.free()
    for material in car.data.materials:
        if material is None:
            continue
        material.use_backface_culling = not is_glass(material.name)
    # One UV set and no vertex colours. The renderer instances a prop by
    # binding its transforms in a second vertex stream, and CNA refuses a
    # mesh whose own stream already carries the usages that stream declares:
    # a second TEXCOORD or a COLOR attribute from Sketchfab's export is
    # exactly that, and the car then draws nothing.
    while len(car.data.uv_layers) > 1:
        car.data.uv_layers.remove(car.data.uv_layers[-1])
    for attribute in list(car.data.color_attributes):
        car.data.color_attributes.remove(attribute)
    objects = [car]

    os.makedirs(args.out, exist_ok=True)
    near = decimate_to(objects, spec["near"])
    near_path = os.path.join(args.out, name + ".glb")
    export(objects, near_path)
    document = patch_glb(near_path)
    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        preview(objects, os.path.join(args.preview, name + ".png"))
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

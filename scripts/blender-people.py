# Generates the hero pedestrians from the MakeHuman base mesh with MPFB, inside
# Blender, and writes them in this project's own character format:
#
#     BLENDER_USER_EXTENSIONS=build/blender/extensions \
#     blender -b --python-use-system-env --python scripts/blender-people.py -- \
#         --downloads assets/external/downloads --work build/blender \
#         --out assets/external/downloads/derived/people [--only person-01 ...]
#
# Why this shape. The crowd's generated figures read as mannequins at four
# metres, and a rigged human under a licence this repository can carry does
# not exist ready-made. MakeHuman's base mesh, its targets and the system
# asset pack (skins, clothes, shoes, hair, eyes) are CC0, and MPFB is the
# Blender extension that assembles them. What MPFB produces is a Blender
# scene: a body with a 137-bone rig, clothes fitted and weighted to it, hair
# and eyes on the head. This script turns that into what the renderer already
# draws -- one skinned mesh per material on the project's nineteen-bone
# skeleton, driven by the walk and idle clips CharacterFactory builds -- by:
#
#   * installing MPFB from the fetched zip into the work directory's extension
#     repository, and the asset pack into MPFB's user data, so nothing touches
#     the user's own Blender configuration;
#   * building each person from the table below through MPFB's own
#     deserializer: macro targets, skin, clothes, shoes, hair, eyes, eyebrows,
#     and the default rig with MakeHuman's authored weights;
#   * posing the arms down from MakeHuman's A-pose, because the project's
#     clips swing the arms from a hanging bind pose;
#   * applying every mask and armature modifier, so the mesh is the posed
#     body with its helpers and the skin under the clothes removed;
#   * folding the rig's vertex groups onto the nineteen bones -- every finger
#     onto the hand, both twist bones onto the limb, the face onto the head --
#     and placing the nineteen joints at the rig's own joints;
#   * writing the textures beside the mesh, capped at 2k for skin and 1k for
#     the rest, and the mesh as a JSON header plus a binary of vertices in the
#     renderer's skinned layout, at a near and a decimated far weight.
#
# The seed and the table make it deterministic; the same inputs give the
# same people.
import argparse
import json
import os
import struct
import sys
import zipfile

import bpy
import mathutils
import numpy as np

# --- the people ---------------------------------------------------------------
# Eight, one per crowd variant. Macro values are MakeHuman's 0..1 sliders.
PEOPLE = [
    dict(name="person-01", gender=0.05, age=0.42, weight=0.48, muscle=0.5, height=0.55,
         race=dict(caucasian=0.9, asian=0.05, african=0.05),
         skin="young_caucasian_female", clothes=["female_elegantsuit01", "shoes03"],
         hair="bob02", hairTint=[0.55, 0.42, 0.28], eyebrows="eyebrow002",
         tints={"female_elegantsuit01": [0.75, 0.62, 0.62]}),
    dict(name="person-02", gender=0.95, age=0.48, weight=0.55, muscle=0.55, height=0.62,
         race=dict(caucasian=0.85, asian=0.05, african=0.10),
         skin="young_caucasian_male", clothes=["male_casualsuit05", "shoes04"],
         hair="short02", hairTint=[0.30, 0.22, 0.16], eyebrows="eyebrow005",
         tints={"male_casualsuit05": [0.62, 0.66, 0.74]}),
    dict(name="person-03", gender=0.95, age=0.62, weight=0.60, muscle=0.45, height=0.58,
         race=dict(caucasian=0.95, asian=0.03, african=0.02),
         skin="middleage_caucasian_male", clothes=["male_elegantsuit01", "shoes03"],
         hair="short04", hairTint=[0.40, 0.38, 0.36], eyebrows="eyebrow009",
         tints={"male_elegantsuit01": [0.45, 0.46, 0.52]}),
    dict(name="person-04", gender=0.05, age=0.35, weight=0.40, muscle=0.5, height=0.50,
         race=dict(caucasian=0.1, asian=0.85, african=0.05),
         skin="young_asian_female", clothes=["female_casualsuit01", "shoes06"],
         hair="long01", hairTint=[0.12, 0.09, 0.08], eyebrows="eyebrow001",
         tints={"female_casualsuit01": [0.55, 0.30, 0.32]}),
    dict(name="person-05", gender=0.95, age=0.38, weight=0.45, muscle=0.6, height=0.68,
         race=dict(caucasian=0.05, asian=0.05, african=0.90),
         skin="young_african_male", clothes=["male_casualsuit03", "shoes01"],
         hair="afro01", hairTint=[0.10, 0.08, 0.07], eyebrows="eyebrow006",
         tints={"male_casualsuit03": [0.80, 0.82, 0.90]}),
    dict(name="person-06", gender=0.05, age=0.45, weight=0.52, muscle=0.5, height=0.52,
         race=dict(caucasian=0.75, asian=0.05, african=0.20),
         skin="young_caucasian_female2", clothes=["female_sportsuit01", "shoes05"],
         hair="ponytail01", hairTint=[0.35, 0.22, 0.12], eyebrows="eyebrow003",
         tints={"female_sportsuit01": [0.30, 0.32, 0.40]}),
    dict(name="person-07", gender=0.95, age=0.78, weight=0.68, muscle=0.35, height=0.48,
         race=dict(caucasian=0.9, asian=0.05, african=0.05),
         skin="old_caucasian_male", clothes=["male_worksuit01", "shoes02"],
         hair="short03", hairTint=[0.60, 0.58, 0.55], eyebrows="eyebrow011",
         tints={"male_worksuit01": [0.50, 0.55, 0.72]}),
    dict(name="person-08", gender=0.95, age=0.30, weight=0.42, muscle=0.55, height=0.60,
         race=dict(caucasian=0.05, asian=0.90, african=0.05),
         skin="young_asian_male", clothes=["male_casualsuit01", "shoes02"],
         hair="short01", hairTint=[0.08, 0.06, 0.06], eyebrows="eyebrow004",
         tints={"male_casualsuit01": [0.36, 0.40, 0.52]}),
]

# --- the skeleton -------------------------------------------------------------
# The project's nineteen bones in the order CharacterFactory adds them, each
# with the MPFB joint it sits at and the MPFB bones whose weights it takes.
# ".R" here is the +x side, as it is in CharacterFactory; MPFB's ".L" bones
# are on +x because its humans face -Y.
BONES = [
    ("pelvis",     None,       "root",         ["root", "pelvis"]),
    ("spine",      "pelvis",   "spine04",      ["spine05", "spine04"]),
    ("chest",      "spine",    "spine02",      ["spine03", "spine02", "spine01", "breast"]),
    ("neck",       "chest",    "neck01",       ["neck01", "neck02", "neck03"]),
    ("head",       "neck",     "head",         ["head", "jaw", "eye", "levator", "oculi",
                                                "orbicularis", "oris", "risorius",
                                                "temporalis", "special", "tongue"]),
    ("clavicle.R", "chest",    "clavicle.L",   ["clavicle.L", "shoulder01.L"]),
    ("upperarm.R", "clavicle.R", "upperarm01.L", ["upperarm01.L", "upperarm02.L"]),
    ("forearm.R",  "upperarm.R", "lowerarm01.L", ["lowerarm01.L", "lowerarm02.L"]),
    ("hand.R",     "forearm.R", "wrist.L",     ["wrist.L", "finger", "metacarpal"]),
    ("clavicle.L", "chest",    "clavicle.R",   ["clavicle.R", "shoulder01.R"]),
    ("upperarm.L", "clavicle.L", "upperarm01.R", ["upperarm01.R", "upperarm02.R"]),
    ("forearm.L",  "upperarm.L", "lowerarm01.R", ["lowerarm01.R", "lowerarm02.R"]),
    ("hand.L",     "forearm.L", "wrist.R",     ["wrist.R", "finger", "metacarpal"]),
    ("thigh.R",    "pelvis",   "upperleg01.L", ["upperleg01.L", "upperleg02.L"]),
    ("shin.R",     "thigh.R",  "lowerleg01.L", ["lowerleg01.L", "lowerleg02.L"]),
    ("foot.R",     "shin.R",   "foot.L",       ["foot.L", "toe"]),
    ("thigh.L",    "pelvis",   "upperleg01.R", ["upperleg01.R", "upperleg02.R"]),
    ("shin.L",     "thigh.L",  "lowerleg01.R", ["lowerleg01.R", "lowerleg02.R"]),
    ("foot.L",     "shin.L",   "foot.R",       ["foot.R", "toe"]),
]


def log(*args):
    print("people:", *args, flush=True)


def bone_for_group(group):
    """Which of the nineteen bones an MPFB vertex group folds onto."""
    side = ".L" if group.endswith(".L") else ".R" if group.endswith(".R") else ""
    for name, _, _, takes in BONES:
        for pattern in takes:
            if pattern.endswith(".L") or pattern.endswith(".R"):
                if group == pattern:
                    return name
            elif group.startswith(pattern):
                # A sided pattern without a side: fingers, toes, the face.
                if name.endswith(".R") or name.endswith(".L"):
                    if side == ".L" and name.endswith(".R"):
                        return name
                    if side == ".R" and name.endswith(".L"):
                        return name
                    continue
                return name
    return None


# --- MPFB ---------------------------------------------------------------------

def install_mpfb(args):
    ext_root = os.environ.get("BLENDER_USER_EXTENSIONS")
    if not ext_root:
        raise SystemExit("set BLENDER_USER_EXTENSIONS to <work>/extensions before running")
    repo = os.path.join(ext_root, "user_default")
    target = os.path.join(repo, "mpfb")
    zip_path = os.path.join(args.downloads, "makehuman", "add-on-mpfb-v2.0.17.zip")
    if not os.path.isfile(os.path.join(target, "blender_manifest.toml")):
        os.makedirs(target, exist_ok=True)
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(target)
        log("installed MPFB into", target)
    # MPFB keeps its user data under the extension repository's .user folder
    # when no preference says otherwise; the pack goes there.
    data = os.path.join(ext_root, ".user", "user_default", "mpfb", "data")
    if not os.path.isdir(os.path.join(data, "clothes")):
        os.makedirs(data, exist_ok=True)
        with zipfile.ZipFile(os.path.join(args.downloads, "makehuman",
                                          "makehuman_system_assets_cc0.zip")) as z:
            z.extractall(data)
        log("unpacked the MakeHuman system assets into", data)
    bpy.ops.preferences.addon_enable(module="bl_ext.user_default.mpfb")


def build_human(person):
    from bl_ext.user_default.mpfb.services.humanservice import HumanService
    from bl_ext.user_default.mpfb.services.objectservice import ObjectService

    info = HumanService._create_default_human_info_dict()
    info["name"] = person["name"]
    info["phenotype"] = {
        "gender": person["gender"], "age": person["age"], "muscle": person["muscle"],
        "weight": person["weight"], "proportions": 0.5, "height": person["height"],
        "cupsize": 0.5, "firmness": 0.5, "race": person["race"],
    }
    info["rig"] = "default_no_toes"
    info["eyes"] = "low-poly.mhclo"
    info["eyebrows"] = person["eyebrows"] + ".mhclo"
    info["hair"] = person["hair"] + ".mhclo"
    info["clothes"] = [c + ".mhclo" for c in person["clothes"]]
    info["skin_mhmat"] = person["skin"] + ".mhmat"
    info["skin_material_type"] = "GAMEENGINE"
    info["clothes_material_type"] = "GAMEENGINE"
    info["eyes_material_type"] = "MAKESKIN"
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    settings["material_instances"] = "NEVER"
    basemesh = HumanService.deserialize_from_dict(info, settings)
    rig = ObjectService.find_object_of_type_amongst_nearest_relatives(basemesh, "Skeleton")
    return basemesh, rig


def relatives(basemesh, rig):
    """Every mesh that belongs to this human: the body and what is parented
    to it or to its rig."""
    out = []
    for o in bpy.context.scene.objects:
        if o.type != "MESH":
            continue
        p = o
        while p is not None:
            if p is basemesh or p is rig:
                out.append(o)
                break
            p = p.parent
    if basemesh not in out:
        out.append(basemesh)
    return out


def pose_arms_down(rig):
    """From MakeHuman's A-pose to a hanging arm, about the shoulder."""
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="POSE")
    for side, sign in (("L", 1.0), ("R", -1.0)):
        pb = rig.pose.bones.get("upperarm01." + side)
        if pb is None:
            continue
        head = rig.matrix_world @ pb.head
        tail = rig.matrix_world @ pb.tail
        current = (tail - head).normalized()
        target = mathutils.Vector((sign * 0.10, 0.03, -1.0)).normalized()
        q = current.rotation_difference(target)
        rotate = (mathutils.Matrix.Translation(head) @ q.to_matrix().to_4x4()
                  @ mathutils.Matrix.Translation(-head))
        pb.matrix = rig.matrix_world.inverted() @ rotate @ rig.matrix_world @ pb.matrix
        bpy.context.view_layer.update()
        # A little bend at the elbow, forward, the way an arm hangs.
        lower = rig.pose.bones.get("lowerarm01." + side)
        if lower is not None:
            head = rig.matrix_world @ lower.head
            bend = mathutils.Quaternion((1.0, 0.0, 0.0), -0.18)
            rotate = (mathutils.Matrix.Translation(head) @ bend.to_matrix().to_4x4()
                      @ mathutils.Matrix.Translation(-head))
            lower.matrix = rig.matrix_world.inverted() @ rotate @ rig.matrix_world @ lower.matrix
            bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")


def apply_modifiers(meshes):
    for o in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.context.view_layer.objects.active = o
        # MakeHuman's targets are shape keys, and Blender applies no modifier
        # to a mesh that has any. Baking the mix into the base mesh is the
        # figure as MPFB shows it; nothing after this needs the sliders.
        if o.data.shape_keys is not None:
            bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
        for mod in list(o.modifiers):
            if mod.type in ("MASK", "ARMATURE"):
                try:
                    bpy.ops.object.modifier_apply(modifier=mod.name)
                except RuntimeError as failure:
                    log("  could not apply", mod.name, "on", o.name, failure)
            else:
                o.modifiers.remove(mod)


def joint(rig, name):
    pb = rig.pose.bones.get(name)
    if pb is None:
        raise SystemExit(f"rig has no bone {name}")
    return rig.matrix_world @ pb.head


def to_app(v):
    """Blender (x, y, z), z up, facing -Y  ->  the renderer's (x, y, z), y up,
    facing +Z. The same mapping the glTF exporter uses."""
    return (v.x, v.z, -v.y)


# --- materials ----------------------------------------------------------------

def image_nodes(material):
    out = {}
    if material is None or material.node_tree is None:
        return out
    for node in material.node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image is not None:
            name = (node.image.name + " " + node.name + " " + node.label).lower()
            if "normal" in name:
                out.setdefault("normal", node.image)
            else:
                out.setdefault("albedo", node.image)
    return out


def save_image(image, path, limit):
    """Writes a copy of @p image as PNG, no larger than @p limit."""
    copy = image.copy()
    if max(copy.size) > limit:
        s = limit / max(copy.size)
        copy.scale(max(1, int(copy.size[0] * s)), max(1, int(copy.size[1] * s)))
    copy.filepath_raw = path
    copy.file_format = "PNG"
    copy.save()
    bpy.data.images.remove(copy)


def has_alpha(image):
    pixels = np.array(image.pixels[:], dtype=np.float32)
    if pixels.size == 0:
        return False
    alpha = pixels[3::4]
    return bool(np.mean(alpha < 0.5) > 0.01)


# --- export -------------------------------------------------------------------

def export_part(o, bone_index, group_to_bone):
    """One mesh object -> vertices in the renderer's skinned layout and
    triangle indices, both as numpy arrays."""
    mesh = o.data
    mesh.calc_loop_triangles()
    uv_layer = mesh.uv_layers.active
    if uv_layer is None:
        log("  no UVs on", o.name)
        return None, None
    uv_name = uv_layer.name
    try:
        mesh.calc_tangents(uvmap=uv_name)
    except RuntimeError:
        # Meshes with n-gons that the tangent calculator refuses.
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.quads_convert_to_tris()
        bpy.ops.object.mode_set(mode="OBJECT")
        mesh = o.data
        mesh.calc_loop_triangles()
        mesh.calc_tangents(uvmap=uv_name)
    # Re-fetched: calc_tangents adds custom-data layers, and a UV layer
    # reference taken before it points at freed memory afterwards -- most
    # loops then read some other array as their UVs, silently.
    uv_layer = mesh.uv_layers[uv_name]
    world = o.matrix_world
    normal_world = world.to_3x3().inverted().transposed()

    # Weights per vertex, folded onto the nineteen bones.
    vcount = len(mesh.vertices)
    weights = np.zeros((vcount, len(BONES)), dtype=np.float32)
    names = [g.name for g in o.vertex_groups]
    for vi, v in enumerate(mesh.vertices):
        for g in v.groups:
            bone = group_to_bone.get(names[g.group]) if g.group < len(names) else None
            if bone is None:
                continue
            weights[vi, bone_index[bone]] += g.weight
    # A vertex nothing claims goes to the nearest of pelvis/head by height,
    # which is where an unweighted eyelash or a stray sole ends up anyway.
    unclaimed = weights.sum(axis=1) <= 1e-6
    for vi in np.nonzero(unclaimed)[0]:
        z = (world @ mesh.vertices[vi].co).z
        weights[vi, bone_index["head" if z > 1.3 else "pelvis"]] = 1.0

    loops = mesh.loops
    uvs = uv_layer.data
    tri_count = len(mesh.loop_triangles)
    verts = np.zeros((tri_count * 3, 17), dtype=np.float32)   # 68 bytes
    indices = np.zeros((tri_count * 3,), dtype=np.uint32)
    row = 0
    for tri in mesh.loop_triangles:
        # Second and third corner swapped: Blender winds a front face
        # counter-clockwise and the renderer's cull keeps clockwise ones
        # (docs/cna-findings.md CNA-F5, CNA-F15). The normals are the
        # author's either way.
        for li in (tri.loops[0], tri.loops[2], tri.loops[1]):
            loop = loops[li]
            vi = loop.vertex_index
            p = world @ mesh.vertices[vi].co
            n = (normal_world @ loop.normal).normalized()
            t = (normal_world @ loop.tangent).normalized()
            if t.length < 0.5 or t.dot(t) != t.dot(t):
                # A degenerate UV triangle leaves Blender with no tangent;
                # any unit vector across the normal keeps the vertex well
                # formed, and the shader never sees the difference on a
                # face that has no texture area anyway.
                axis = mathutils.Vector((1.0, 0.0, 0.0)) if abs(n.x) < 0.9 else mathutils.Vector((0.0, 1.0, 0.0))
                t = n.cross(axis).normalized()
            uv = uvs[li].uv
            w = weights[vi]
            top = np.argsort(w)[::-1][:4]
            wt = w[top]
            total = float(wt.sum())
            wt = wt / total if total > 0 else np.array([1, 0, 0, 0], dtype=np.float32)
            verts[row, 0:3] = to_app(p)
            verts[row, 3:6] = to_app(n)
            verts[row, 6:9] = to_app(t)
            verts[row, 9] = 1.0 if loop.bitangent_sign >= 0.0 else -1.0
            verts[row, 10:12] = (uv.x, 1.0 - uv.y)
            verts[row, 12:16] = wt
            # Bone indices packed as four bytes in one float32 slot.
            packed = int(top[0]) | (int(top[1]) << 8) | (int(top[2]) << 16) | (int(top[3]) << 24)
            verts[row, 16] = np.frombuffer(struct.pack("<I", packed), dtype=np.float32)[0]
            indices[row] = row
            row += 1
    mesh.free_tangents()
    # Weld identical rows: three loops of a smooth vertex are one vertex.
    unique, inverse = np.unique(verts, axis=0, return_inverse=True)
    indices = inverse.astype(np.uint32).reshape(-1)
    return unique, indices


def export_person(person, basemesh, rig, meshes, args, level, bin_path, header):
    bone_index = {b[0]: i for i, b in enumerate(BONES)}
    group_to_bone = {}
    for o in meshes:
        for g in o.vertex_groups:
            if g.name not in group_to_bone:
                group_to_bone[g.name] = bone_for_group(g.name)
    parts = []
    blob = bytearray()
    for o in meshes:
        kind = "body" if o is basemesh else str(o.name.split(".")[-1]).lower()
        material = o.active_material
        images = image_nodes(material)
        if not images.get("albedo"):
            log("  skipping", o.name, "without an albedo")
            continue
        verts, indices = export_part(o, bone_index, group_to_bone)
        if verts is None or len(indices) == 0:
            continue
        asset = kind if o is basemesh else kind
        # Textures are shared between people who wear the same thing: named
        # by the asset, written once.
        texture_stem = "people-" + (person["skin"] if o is basemesh else asset)
        albedo_path = os.path.join(args.out, texture_stem + ".albedo.png")
        limit = 2048 if o is basemesh else 1024
        if not os.path.exists(albedo_path):
            save_image(images["albedo"], albedo_path, limit)
        normal_name = None
        if images.get("normal"):
            normal_path = os.path.join(args.out, texture_stem + ".normal.png")
            if not os.path.exists(normal_path):
                save_image(images["normal"], normal_path, limit)
            normal_name = texture_stem + ".normal"
        masked = (not o is basemesh) and not asset.startswith("low-poly") and has_alpha(images["albedo"])
        tint = [1.0, 1.0, 1.0]
        for key, value in person.get("tints", {}).items():
            if key == asset:
                tint = value
        if o is not basemesh and "hair" in kind or asset == person["hair"]:
            tint = person.get("hairTint", tint)
        roughness = 0.55 if o is basemesh else 0.30 if "eye" in asset and "brow" not in asset else 0.62 if asset == person["hair"] else 0.85
        part = {
            "name": o.name,
            "kind": "body" if o is basemesh else ("hair" if asset == person["hair"] else
                    "eyes" if asset.startswith("low-poly") else
                    "eyebrows" if asset.startswith("eyebrow") else "clothes"),
            "albedo": texture_stem + ".albedo",
            "normal": normal_name,
            "baseColour": tint,
            "roughness": roughness,
            "metallic": 0.0,
            "alphaMode": "MASK" if masked else "OPAQUE",
            "doubleSided": masked,
            "vertexOffset": len(blob),
            "vertexCount": int(len(verts)),
            "indexOffset": 0,
            "indexCount": int(len(indices)),
        }
        blob += verts.astype("<f4").tobytes()
        part["indexOffset"] = len(blob)
        blob += indices.astype("<u4").tobytes()
        parts.append(part)
        log(f"  {level} part {o.name}: {len(verts)} vertices, {len(indices) // 3} triangles, "
            f"{part['kind']}, {part['alphaMode']}")
    with open(bin_path, "wb") as f:
        f.write(blob)
    header[level] = {"file": os.path.basename(bin_path), "parts": parts}


def decimate(meshes, ratio):
    for o in meshes:
        if len(o.data.polygons) < 400:
            continue
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.context.view_layer.objects.active = o
        mod = o.modifiers.new("decimate", "DECIMATE")
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=mod.name)


def process(person, args):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    basemesh, rig = build_human(person)
    if rig is None:
        raise SystemExit("MPFB built no rig")
    meshes = relatives(basemesh, rig)
    log(person["name"], "built:", [o.name for o in meshes])
    pose_arms_down(rig)
    # The joints, read from the posed rig before the modifiers bake the pose
    # into the meshes.
    bpy.context.view_layer.update()
    joints = {}
    for name, parent, mpfb, _ in BONES:
        joints[name] = to_app(joint(rig, mpfb))
    # The head bone sits at the base of the skull; the project's head joint
    # is its centre.
    head = list(joints["head"])
    head[1] += 0.09
    joints["head"] = tuple(head)
    apply_modifiers(meshes)
    lowest = min((o.matrix_world @ mathutils.Vector(c)).z for o in meshes for c in o.bound_box)
    highest = max((o.matrix_world @ mathutils.Vector(c)).z for o in meshes for c in o.bound_box)
    log(f"  stands {highest - lowest:.3f} m tall, feet at z = {lowest:.3f}")

    os.makedirs(args.out, exist_ok=True)
    header = {
        "name": person["name"],
        "height": round(highest - lowest, 4),
        "bones": [{"name": name, "parent": parent, "head": [round(c, 5) for c in joints[name]]}
                  for name, parent, _, _ in BONES],
        "source": "MakeHuman system assets (CC0) through MPFB; see assets/external/manifest.json",
    }
    export_person(person, basemesh, rig, meshes, args, "near",
                  os.path.join(args.out, person["name"] + ".bin"), header)
    decimate(meshes, 0.35)
    export_person(person, basemesh, rig, meshes, args, "far",
                  os.path.join(args.out, person["name"] + "-far.bin"), header)
    with open(os.path.join(args.out, person["name"] + ".json"), "w") as f:
        json.dump(header, f, indent=1)
    log(person["name"], "written")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--downloads", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--only", nargs="*", default=[])
    parser.add_argument("--probe-uvs", action="store_true",
                        help="build one person, print every mesh's UV layers, and stop")
    args = parser.parse_args(argv)
    install_mpfb(args)
    if args.probe_uvs:
        basemesh, rig = build_human(next((p for p in PEOPLE if p["name"] in args.only), PEOPLE[0]))
        for o in relatives(basemesh, rig):
            for layer in o.data.uv_layers:
                us = [d.uv.x for d in layer.data]
                vs = [d.uv.y for d in layer.data]
                log(f"UV {o.name} layer '{layer.name}' active={layer.active} "
                    f"render={layer.active_render} u {min(us):.2f}..{max(us):.2f} "
                    f"v {min(vs):.2f}..{max(vs):.2f} loops {len(us)}")
        bone_index = {b[0]: i for i, b in enumerate(BONES)}
        body = relatives(basemesh, rig)[0]
        verts, indices = export_part(body, bone_index, {})
        log("exported body uv u", verts[:, 10].min(), verts[:, 10].max(), "v", verts[:, 11].min(),
            verts[:, 11].max(), "tangent w", set(verts[:, 9].tolist()))
        return
    for person in PEOPLE:
        if args.only and person["name"] not in args.only:
            continue
        process(person, args)


main()

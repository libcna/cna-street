# Builds street-weight levels of detail of a Poly Haven tree and exports them
# as glTF, from inside Blender:
#
#   blender -b <tree>.blend --python scripts/blender-tree-lod.py -- \
#           --object tree_small_02_LOD1 --out derived/tree_small_02_street.glb \
#           --far-out derived/tree_small_02_far.glb
#
# Poly Haven publishes its trees at two levels: LOD0 with every leaf modelled
# (two million triangles) and LOD1 at half a million. Neither is a street
# asset. This takes LOD1, separates the wood from the leaves, decimates the
# wood, keeps a random fraction of the leaf clusters and scales the survivors
# up so the crown keeps its density and silhouette, and writes one glTF with
# the leaves as an alpha-masked material -- twice, at a near and a far weight,
# so the far copy can stand in past the switch distance without the crown
# changing shape. The seed is fixed, so the same input gives the same output.
import argparse
import pathlib
import random
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
parser = argparse.ArgumentParser()
parser.add_argument("--object", required=True)
# A tree published as glTF rather than .blend: imported first, and the
# object named is the one the importer makes (the LOD0 node's name).
parser.add_argument("--gltf", default="")
parser.add_argument("--out", required=True)
parser.add_argument("--far-out", default="")
parser.add_argument("--keep-leaves", type=float, default=0.40)
parser.add_argument("--leaf-scale", type=float, default=1.40)
parser.add_argument("--wood-ratio", type=float, default=0.30)
parser.add_argument("--far-keep-leaves", type=float, default=0.10)
parser.add_argument("--far-leaf-scale", type=float, default=2.60)
parser.add_argument("--far-wood-ratio", type=float, default=0.06)
parser.add_argument("--leaf-material", default="leaves")
parser.add_argument("--seed", type=int, default=20260903)
args = parser.parse_args(argv)


def log(*parts):
    print("LOD:", *parts, flush=True)


def is_leaves(o):
    return any(args.leaf_material in (m.name if m else "") for m in o.data.materials)


def select_only(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


if args.gltf:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=args.gltf)
    for o in list(bpy.context.scene.objects):
        if o.type == "MESH":
            o.select_set(True)
            bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
src = bpy.data.objects[args.object]
for o in bpy.data.objects:
    o.hide_set(False)
    o.hide_viewport = False
select_only([src])
# Everything else goes: the export is of the selection, and a stray leaf
# cluster or the geometry-nodes curve would ride along.
for o in list(bpy.data.objects):
    if o is not src:
        bpy.data.objects.remove(o, do_unlink=True)

# Wood and leaves apart, by material.
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.separate(type='MATERIAL')
bpy.ops.object.mode_set(mode='OBJECT')
pieces = list(bpy.context.selected_objects)
leaves = [o for o in pieces if is_leaves(o)]
wood = [o for o in pieces if o not in leaves]
# A fixed order, so both exports lay their primitives out the same way and a
# part-for-part level-of-detail swap matches.
wood.sort(key=lambda o: o.data.materials[0].name if o.data.materials and o.data.materials[0] else o.name)
leaves.sort(key=lambda o: o.name)
log("pieces", [(o.name, len(o.data.polygons)) for o in wood + leaves])

# The leaf clusters, found once: every cluster the geometry nodes scattered is
# a loose island in the baked mesh.
islands_by_source = {}
for o in leaves:
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.verts.ensure_lookup_table()
    seen = set()
    islands = []
    for v in bm.verts:
        if v.index in seen:
            continue
        stack = [v.index]
        island = []
        seen.add(v.index)
        while stack:
            cur = bm.verts[stack.pop()]
            island.append(cur.index)
            for e in cur.link_edges:
                other = e.other_vert(cur).index
                if other not in seen:
                    seen.add(other)
                    stack.append(other)
        islands.append(island)
    bm.free()
    islands_by_source[o.name] = islands
    log("leaves", o.name, "islands", len(islands))


def build(level, keep_leaves, leaf_scale, wood_ratio, out):
    # Copies of every piece, so the far level starts from the same source.
    copies = []
    for o in wood + leaves:
        c = o.copy()
        c.data = o.data.copy()
        c.name = f"{level}_{o.name}"
        bpy.context.collection.objects.link(c)
        copies.append((o, c))

    for source, o in copies:
        if is_leaves(o):
            continue
        select_only([o])
        before = len(o.data.polygons)
        mod = o.modifiers.new("decimate", 'DECIMATE')
        mod.ratio = wood_ratio
        mod.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=mod.name)
        log(level, "wood", o.name, before, "->", len(o.data.polygons))

    rng = random.Random(args.seed)
    for source, o in copies:
        if not is_leaves(o):
            continue
        islands = islands_by_source[source.name]
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bm.verts.ensure_lookup_table()
        doomed = []
        kept = 0
        for island in islands:
            if rng.random() > keep_leaves:
                doomed.extend(bm.verts[i] for i in island)
                continue
            kept += 1
            verts = [bm.verts[i] for i in island]
            centre = sum((v.co for v in verts), Vector()) / len(verts)
            for v in verts:
                v.co = centre + (v.co - centre) * leaf_scale
        before = len(bm.faces)
        # One delete for the whole mesh: a delete per island walks the mesh
        # each time and turns a minute into an afternoon.
        if doomed:
            bmesh.ops.delete(bm, geom=doomed, context='VERTS')
        bm.to_mesh(o.data)
        bm.free()
        log(level, "leaves", o.name, "kept", kept, "of", len(islands), "faces", before, "->",
            len(o.data.polygons))
        for m in o.data.materials:
            if m is not None:
                m.blend_method = 'CLIP'
                m.alpha_threshold = 0.5

    ordered = [c for _, c in copies]
    select_only(ordered)
    bpy.ops.object.join()
    tree = bpy.context.view_layer.objects.active
    tree.name = f"street_tree_{level}"
    # One UV set and no vertex colours: the renderer instances a tree by
    # binding its transforms in a second vertex stream, and CNA refuses a
    # mesh whose own stream already carries the usages that stream declares.
    # A scan's LOD0 arrives with a colour attribute for its wind rig.
    while len(tree.data.uv_layers) > 1:
        tree.data.uv_layers.remove(tree.data.uv_layers[-1])
    for attribute in list(tree.data.color_attributes):
        try:
            tree.data.color_attributes.remove(attribute)
        except RuntimeError:
            # An unnamed attribute cannot be removed this way; the exporter is
            # told to write no colours either way.
            pass
    log(level, "total faces", len(tree.data.polygons))
    select_only([tree])
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True,
                              export_apply=True, export_yup=True, export_texcoords=True,
                              export_normals=True, export_tangents=True, export_materials='EXPORT',
                              export_image_format='AUTO', export_cameras=False, export_lights=False,
                              export_animations=False, export_skins=False,
                              export_vertex_color='NONE', export_attributes=False)
    log(level, "wrote", out)
    bpy.data.objects.remove(tree, do_unlink=True)
    # Blender 4.2+ writes every material with alpha as BLEND whatever the
    # render method says; the leaves have to be MASK or the crown is a ghost.
    import subprocess
    subprocess.run([sys.executable if "python" in sys.executable else "python3",
                    str(pathlib.Path(__file__).with_name("glb-mask-leaves.py")), out,
                    args.leaf_material], check=False)


build("near", args.keep_leaves, args.leaf_scale, args.wood_ratio, args.out)
if args.far_out:
    build("far", args.far_keep_leaves, args.far_leaf_scale, args.far_wood_ratio, args.far_out)

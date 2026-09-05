# Renders orientation previews of a vehicle model, inside Blender:
#
#     blender -b --python scripts/blender-vehicle-preview.py -- model.glb out.png
#
# Imports the file, prints every object's bounds so a backdrop or a shadow
# plane can be told from the car, then renders a top view and a three-quarter
# view with the Workbench engine. The point is to find out which way the car
# is facing before scripts/blender-vehicles.py commits to a rotation.
import bpy, sys, math, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
source, out = argv[0], argv[1]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=source)

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

lo = mathutils.Vector((1e9, 1e9, 1e9)); hi = mathutils.Vector((-1e9, -1e9, -1e9))
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ mathutils.Vector(c)
        lo = mathutils.Vector(map(min, lo, w)); hi = mathutils.Vector(map(max, hi, w))
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
    mats = ",".join(m.name for m in o.data.materials if m)
    olo = mathutils.Vector((1e9,) * 3); ohi = mathutils.Vector((-1e9,) * 3)
    for c in o.bound_box:
        w = o.matrix_world @ mathutils.Vector(c)
        olo = mathutils.Vector(map(min, olo, w)); ohi = mathutils.Vector(map(max, ohi, w))
    print(f"OBJ {o.name:40s} tris={tris:7d} size=({(ohi-olo).x:.2f},{(ohi-olo).y:.2f},{(ohi-olo).z:.2f}) "
          f"lo=({olo.x:.2f},{olo.y:.2f},{olo.z:.2f}) mats={mats}")
size = hi - lo
print(f"BOUNDS lo=({lo.x:.2f},{lo.y:.2f},{lo.z:.2f}) hi=({hi.x:.2f},{hi.y:.2f},{hi.z:.2f}) size=({size.x:.2f},{size.y:.2f},{size.z:.2f})")
centre = (lo + hi) * 0.5

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "MATERIAL"
scene.render.resolution_x = 960
scene.render.resolution_y = 540
scene.render.film_transparent = False
cam_data = bpy.data.cameras.new("cam"); cam_data.type = "ORTHO"
cam = bpy.data.objects.new("cam", cam_data); scene.collection.objects.link(cam); scene.camera = cam
radius = max(size.x, size.y, size.z)

def shoot(direction, up, name, ortho):
    cam_data.ortho_scale = ortho
    d = mathutils.Vector(direction).normalized()
    cam.location = centre + d * radius * 3
    cam.rotation_euler = d.to_track_quat("Z", up).to_euler()
    scene.render.filepath = out.replace(".png", f"-{name}.png")
    bpy.ops.render.render(write_still=True)

shoot((0, 0, 1), "Y", "top", max(size.x, size.y) * 1.15)     # +X right, +Y up in the image
shoot((1, -1, 0.6), "Y", "front-right", radius * 1.2)        # from +X, -Y, above
shoot((-1, 0, 0), "Y", "side-from-minus-x", max(size.y, size.z) * 1.15)

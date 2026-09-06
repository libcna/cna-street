# Which way does an authored car face?
#
#     python3 scripts/vehicle-orientation.py [--sheet <out.png>] [--check] [name ...]
#
# Every derived vehicle under assets/external/downloads/derived/vehicles must
# leave scripts/blender-vehicles.py in one canonical pose: standing on y = 0,
# centred on x and z, its length along Z, and **its nose toward +Z** -- the
# direction Geometry::Place calls a heading of zero and the direction every
# lofted vehicle and every lane in TrafficSystem is built for. A model that
# arrives from its author facing the other way and is not turned drives
# permanently in reverse, which is exactly what a hand-set `flip` flag in the
# script's table let happen: the flag is a claim about the file, nobody checks
# it, and a wrong claim is invisible until a car is watched from the kerb.
#
# So the claim is checked here, from the geometry alone and with no Blender:
#
#   * `front_sign` reads the body's own shape. A car's cabin sits behind its
#     bonnet, and the deck ahead of the windscreen is longer than the deck
#     behind the backlight -- on a saloon, on a hatchback, on a coupe and on a
#     van alike. Two independent measurements of that (where the tall part of
#     the body sits along the length, and how much low body there is at each
#     end) vote, and the script says how strongly they agreed.
#   * `--sheet` draws every model in side view, nose expected to the right, so
#     the answer can be looked at rather than believed.
#   * `steering_side` reads the same body for the *other* thing a pose has to
#     agree with the street about: which side of its centre line the steering
#     wheel is on. `CityScene::driverSeat` puts the driver there, and it used
#     to put them at -X on the strength of a comment -- so every car on the
#     street was driven from the passenger seat, with the wheel beside the
#     figure and nobody behind it. It is the same class of fault as the flip
#     flag: a claim about a file that nothing measured.
#   * `--check` is the test: it fails when any derived model faces -Z, steers
#     its rear axle, or measures right-hand drive on a street laid out for
#     right-hand traffic.
#
# Pure Python: struct, numpy and (for the sheet) PIL. Nothing here imports bpy,
# because a test that needs Blender is a test nobody runs.
import argparse
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vehicle_pose   # noqa: E402  -- the shared nose-direction rule

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DERIVED = os.path.join(ROOT, "assets", "external", "downloads", "derived", "vehicles")

# How large an area asymmetry counts as evidence of a steering wheel. Below
# this a model simply has no cabin worth measuring -- two of these eight are
# shells with a dashboard and no column -- and saying so is better than
# reading a side out of noise.
STEERING_FLOOR = 0.05

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
DTYPES = {5120: "i1", 5121: "u1", 5122: "i2", 5123: "u2", 5125: "u4", 5126: "f4"}


def read_glb(path):
    """The JSON chunk and the binary chunk of a .glb."""
    with open(path, "rb") as handle:
        blob = handle.read()
    magic, _version, _length = struct.unpack_from("<III", blob, 0)
    if magic != 0x46546C67:
        raise ValueError(f"{path}: not a glb")
    offset = 12
    document, binary = None, b""
    while offset + 8 <= len(blob):
        size, kind = struct.unpack_from("<II", blob, offset)
        chunk = blob[offset + 8: offset + 8 + size]
        if kind == 0x4E4F534A:
            document = json.loads(chunk.decode("utf-8"))
        elif kind == 0x004E4942:
            binary = chunk
        offset += 8 + size + (-size % 4)
    if document is None:
        raise ValueError(f"{path}: no JSON chunk")
    return document, binary


def accessor(document, binary, index):
    """One accessor as a numpy array, interleaving honoured."""
    acc = document["accessors"][index]
    count = acc["count"]
    width = COMPONENTS[acc["type"]]
    dtype = np.dtype(DTYPES[acc["componentType"]]).newbyteorder("<")
    if "bufferView" not in acc:
        return np.zeros((count, width), dtype=dtype)
    view = document["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or width * dtype.itemsize
    raw = np.frombuffer(binary, dtype="u1", count=stride * (count - 1) + width * dtype.itemsize,
                        offset=start)
    rows = np.lib.stride_tricks.as_strided(
        raw, shape=(count, width * dtype.itemsize), strides=(stride, 1)).copy()
    return rows.view(dtype).reshape(count, width)


def node_matrix(node):
    """A node's local transform, TRS or matrix, as a 4x4 row-vector matrix."""
    if "matrix" in node:
        return np.array(node["matrix"], dtype="f8").reshape(4, 4)
    out = np.eye(4)
    if "scale" in node:
        out = np.diag(list(node["scale"]) + [1.0]) @ out
    if "rotation" in node:
        x, y, z, w = node["rotation"]
        rot = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0],
            [0.0, 0.0, 0.0, 1.0]])
        out = out @ rot
    if "translation" in node:
        shift = np.eye(4)
        shift[3, :3] = node["translation"]
        out = out @ shift
    return out


def parts(path):
    """Every primitive as (node name, positions in scene space, triangle indices)."""
    document, binary = read_glb(path)
    out = []

    def walk(index, parent):
        node = document["nodes"][index]
        world = node_matrix(node) @ parent
        if "mesh" in node:
            for primitive in document["meshes"][node["mesh"]].get("primitives", []):
                if "POSITION" not in primitive.get("attributes", {}):
                    continue
                points = accessor(document, binary, primitive["attributes"]["POSITION"]).astype("f8")
                homogeneous = np.hstack([points, np.ones((len(points), 1))])
                indices = (accessor(document, binary, primitive["indices"]).reshape(-1)
                           if "indices" in primitive else np.arange(len(points)))
                out.append((node.get("name", f"node{index}"),
                            (homogeneous @ world)[:, :3], indices.astype("i8")))
        for child in node.get("children", []):
            walk(child, world)

    scene = document.get("scenes", [{}])[document.get("scene", 0)]
    for index in scene.get("nodes", range(len(document.get("nodes", [])))):
        walk(index, np.eye(4))
    return out


def body_points(path):
    """The body's primitives -- everything that is not a wheel -- and the wheels."""
    body, wheels = [], {}
    for name, points, indices in parts(path):
        if name.startswith("wheel_"):
            wheels.setdefault(name[:8], []).append(points)
        else:
            body.append((points, indices))
    if not body:            # the far copy is one welded mesh
        body = [(points, indices) for _n, points, indices in parts(path)]
        wheels = {}
    return body, {key: np.vstack(value) for key, value in wheels.items()}


def front_sign(pieces):
    """+1 when the nose is toward +Z, -1 when it is toward -Z, from the shape.

    @p pieces is a list of (points, indices) for the body's primitives. The
    rule is scripts/vehicle_pose.py, the same one scripts/blender-vehicles.py
    decides the exported pose with.
    """
    along, up, area = [], [], []
    for points, indices in pieces:
        if len(indices) < 3:
            continue
        centroid, size = vehicle_pose.triangle_centroids(points, indices)
        along.append(centroid[:, 2])
        up.append(centroid[:, 1])
        area.append(size)
    if not along:
        return 0.0, 0.0, 0.0
    # front_score answers for the *negative* end of the axis it is given, so
    # the length axis is handed over reversed: a positive score is a nose
    # toward +Z, which is the pose this project requires.
    score, cabin, deck = vehicle_pose.front_score(-np.concatenate(along), np.concatenate(up),
                                                  np.concatenate(area))
    return (1.0 if score >= 0.0 else -1.0), cabin, deck


def steering_side(pieces):
    """Which side of its centre line a car's steering wheel is on.

    Returns (bias, samples). A positive bias means the car's **left** -- +X,
    with the nose at +Z and +Y up, which is the side a left-hand-drive car
    steers from and the side a street with right-hand traffic wants. Near zero
    means the model carries too little cabin to tell, which several of these
    do; the caller reports that rather than guessing.

    The measurement is an area asymmetry, not a search for a torus. A cabin is
    symmetric except for what is in front of the driver -- the wheel, the
    column, the cluster, the pedal box -- so weighing the triangle area on
    each side of the centre line through the dashboard slab answers the
    question without needing to recognise any part by name. The slab is the
    front of the cabin at dash height, and only the *inner* two thirds of the
    half-width is counted: the door skins, the mirrors and the glass are
    symmetric and large, and including them buries the signal under them.
    """
    centroid, up, area = [], [], []
    for points, indices in pieces:
        if len(indices) < 3:
            continue
        c, a = vehicle_pose.triangle_centroids(points, indices)
        centroid.append(c)
        area.append(a)
    if not centroid:
        return 0.0, 0
    centroid = np.vstack(centroid)
    area = np.concatenate(area)
    lo, hi = float(centroid[:, 2].min()), float(centroid[:, 2].max())
    floor, roof = float(centroid[:, 1].min()), float(centroid[:, 1].max())
    length, height = hi - lo, roof - floor
    half = max(float(np.abs(centroid[:, 0]).max()), 1e-3)
    if length < 1e-3 or height < 1e-3:
        return 0.0, 0
    across = np.abs(centroid[:, 0])
    pick = ((centroid[:, 2] > lo + 0.42 * length) & (centroid[:, 2] < lo + 0.64 * length)
            & (centroid[:, 1] > floor + 0.32 * height) & (centroid[:, 1] < floor + 0.62 * height)
            & (across > 0.12 * half) & (across < 0.66 * half))
    left = float(area[pick & (centroid[:, 0] > 0.0)].sum())
    right = float(area[pick & (centroid[:, 0] < 0.0)].sum())
    total = left + right
    if total <= 0.0:
        return 0.0, 0
    return (left - right) / total, int(pick.sum())


def silhouette(indices_by_part, width, height, margin=6):
    """A filled side-view silhouette, +Z to the right and +Y up."""
    from PIL import Image, ImageDraw
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    allPoints = np.vstack([p for p, _ in indices_by_part])
    lo = allPoints.min(axis=0)
    hi = allPoints.max(axis=0)
    scale = min((width - 2 * margin) / max(hi[2] - lo[2], 1e-6),
                (height - 2 * margin) / max(hi[1] - lo[1], 1e-6))
    ox = (width - (hi[2] - lo[2]) * scale) * 0.5
    oy = height - margin

    def project(p):
        return (ox + (p[:, 2] - lo[2]) * scale, oy - (p[:, 1] - lo[1]) * scale)

    for points_, indices in indices_by_part:
        sx, sy = project(points_)
        tri = indices.reshape(-1, 3)
        # Thin the fill on very dense meshes: a silhouette does not need
        # every one of a hundred and fifty thousand triangles.
        step = max(1, len(tri) // 60000)
        for a, b, c in tri[::step]:
            draw.polygon([(sx[a], sy[a]), (sx[b], sy[b]), (sx[c], sy[c])], fill=255)
    return image


def sheet(names, path):
    from PIL import Image, ImageDraw
    cell = (520, 240)
    columns = 2
    rows = (len(names) + columns - 1) // columns
    canvas = Image.new("RGB", (cell[0] * columns, (cell[1] + 22) * rows), (250, 250, 250))
    drawing = ImageDraw.Draw(canvas)
    for index, name in enumerate(names):
        source = os.path.join(DERIVED, name + ".glb")
        if not os.path.isfile(source):
            continue
        pieces = [(points, indices) for _n, points, indices in parts(source)]
        image = silhouette(pieces, cell[0], cell[1])
        tile = Image.new("RGB", cell, (255, 255, 255))
        tile.paste(Image.merge("RGB", (image.point(lambda v: 255 - v),) * 3), (0, 0))
        x = (index % columns) * cell[0]
        y = (index // columns) * (cell[1] + 22)
        canvas.paste(tile, (x, y + 22))
        body, _wheels = body_points(source)
        sign, cabinVote, deckVote = front_sign(body)
        drawing.text((x + 6, y + 5),
                     f"{name}   nose {'+Z (right) OK' if sign > 0 else '-Z (left) WRONG'}"
                     f"   cabin {cabinVote:+.2f} deck {deckVote:+.2f}", fill=(0, 0, 0))
        drawing.line([(x + cell[0] - 60, y + 32), (x + cell[0] - 12, y + 32)], fill=(180, 0, 0))
        drawing.polygon([(x + cell[0] - 12, y + 32), (x + cell[0] - 22, y + 27),
                         (x + cell[0] - 22, y + 37)], fill=(180, 0, 0))
    canvas.save(path)
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sheet", default="")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    if not os.path.isdir(DERIVED):
        print("no derived vehicles: nothing to check")
        return 0
    names = args.names or sorted(
        os.path.splitext(f)[0] for f in os.listdir(DERIVED)
        if f.endswith(".glb") and not f.endswith("-far.glb"))
    if not names:
        print("no derived vehicles: nothing to check")
        return 0

    bad = []
    steers_left = 0
    for name in names:
        source = os.path.join(DERIVED, name + ".glb")
        if not os.path.isfile(source):
            print(f"{name}: not derived")
            continue
        body, wheels = body_points(source)
        sign, cabinVote, deckVote = front_sign(body)
        allPoints = np.vstack([p for p, _i in body])
        lo, hi = allPoints.min(axis=0), allPoints.max(axis=0)
        wheelZ = {k: float(np.mean(v[:, 2])) for k, v in sorted(wheels.items())}
        print(f"{name:20s} {hi[2] - lo[2]:5.2f} m long  nose "
              f"{'+Z' if sign > 0 else '-Z'}  (cabin {cabinVote:+.2f}, deck {deckVote:+.2f})"
              + (f"  wheels {', '.join(f'{k[6:]}:{v:+.2f}' for k, v in wheelZ.items())}"
                 if wheelZ else "  no separate wheels"))
        if sign < 0:
            bad.append(name)
            continue
        # The wheels the scene steers are the ones named front, so a model
        # whose front pair sits behind its rear pair steers the back axle
        # through the junction. It is the same error as a reversed body, one
        # step further in, and the only reason it has ever been right is that
        # the body and the wheels are turned together.
        if len(wheelZ) == 4:
            front = 0.5 * (wheelZ["wheel_fl"] + wheelZ["wheel_fr"])
            rear = 0.5 * (wheelZ["wheel_rl"] + wheelZ["wheel_rr"])
            if front <= rear:
                print(f"    {name}: the front wheels are behind the rear ones")
                bad.append(name)
        bias, samples = steering_side(body)
        verdict = ("left-hand drive" if bias > STEERING_FLOOR
                   else "RIGHT-hand drive" if bias < -STEERING_FLOOR
                   else "no cabin detail")
        print(f"    steering bias {bias:+.3f} over {samples} triangles -- {verdict}")
        if bias < -STEERING_FLOOR:
            print(f"    {name}: steers from the car's right, and this street "
                  "keeps right; CityScene::driverSeat would seat its driver "
                  "beside the wheel")
            bad.append(name)
            continue
        if bias > STEERING_FLOOR:
            steers_left += 1
    if args.sheet:
        print("sheet:", sheet(names, args.sheet))
    if bad:
        print(f"FAIL: {len(bad)} model(s) posed wrongly: {', '.join(bad)}")
        return 1 if args.check else 0
    print(f"{len(names)} derived vehicle(s), every nose toward +Z; "
          f"{steers_left} measure left-hand drive and none measures right")
    return 0


sys.exit(main())

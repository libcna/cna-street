#!/usr/bin/env python3
"""Turn a GLB's blended leaf materials into alpha-masked ones, in place.

Blender 4.2 and later export every material with alpha as `alphaMode: BLEND`,
whatever the material's render method says. Blended foliage is drawn in the
transparent pass without depth writes, and a crown of ten thousand leaf cards
blended in submission order over the sky is a ghost, not a tree. Masked
foliage writes depth and casts a shadow, which is what a leaf does.

    glb-mask-leaves.py <file.glb> [substring ...]

Materials whose name contains any of the substrings (default: "leaves") are
set to MASK with a cutoff of 0.5. The GLB is rewritten with the JSON chunk
re-padded, and the binary chunk left untouched.
"""

import json
import pathlib
import struct
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    path = pathlib.Path(sys.argv[1])
    needles = sys.argv[2:] or ["leaves"]
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        print(f"{path}: not a GLB", file=sys.stderr)
        return 1
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        print(f"{path}: first chunk is not JSON", file=sys.stderr)
        return 1
    document = json.loads(data[20:20 + json_length])
    rest = data[20 + json_length:]

    changed = 0
    for material in document.get("materials", []):
        name = material.get("name", "")
        if any(needle in name for needle in needles):
            material["alphaMode"] = "MASK"
            material["alphaCutoff"] = 0.5
            changed += 1

    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((4 - len(encoded) % 4) % 4)
    out = bytearray()
    out += struct.pack("<III", magic, version, 12 + 8 + len(encoded) + len(rest))
    out += struct.pack("<II", len(encoded), json_type)
    out += encoded
    out += rest
    path.write_bytes(out)
    print(f"{path.name}: {changed} material(s) set to MASK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

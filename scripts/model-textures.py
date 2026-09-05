#!/usr/bin/env python3
"""Lists the images a compiled model refers to, each with the colour space
its mip chain must be averaged in.

    scripts/model-textures.py <model.cnj>

prints one `image=srgb` or `image=linear` line per image the model's meshes
name. A base colour or an emissive map is sRGB-encoded and has to be
decoded before its levels are averaged; a normal, a metallic-roughness or an
occlusion map holds numbers, not light, and is averaged as stored. The
content build reads this to compile every imported model's images through
the same mip-chain compiler the catalogue's own surfaces go through, which
is what stands in for the mip chain CNA's importer does not build
(docs/cna-findings.md GLTF-206).
"""
import json
import sys

SRGB = ("texture", "emissiveMap")
LINEAR = ("normalMap", "metallicRoughnessMap", "occlusionMap", "specularMap")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as handle:
        document = json.load(handle)
    spaces = {}
    for mesh in document.get("meshes", []):
        for key, value in mesh.items():
            if not isinstance(value, str):
                continue
            if key in SRGB:
                space = "srgb"
            elif key in LINEAR:
                space = "linear"
            else:
                continue
            # An image used both ways is compiled for colour: a wrongly
            # linear normal map is a subtle error and a wrongly darkened
            # albedo an obvious one.
            if spaces.get(value) != "srgb":
                spaces[value] = space
    for image in sorted(spaces):
        print(f"{image}={spaces[image]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

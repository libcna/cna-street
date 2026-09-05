#!/usr/bin/env python3
"""Read assets/external/manifest.json for the build and the fetch script.

The manifest is the one record of what this project imports, where each file
came from and under what licence. Three things read it -- the fetch script,
the licence validator and the content build -- and this is the one place that
knows its shape, so the three cannot disagree about it.

    manifest-tool.py models       file=name pairs for the content build:
                                  the glTF to compile, and the logical name
                                  the runtime asks ContentManager for
    manifest-tool.py fetch-list   name <tab> url <tab> path <tab> sha256 for
                                  every file of every asset, models and
                                  surfaces alike, relative to downloads/
    manifest-tool.py declared     every declared path relative to downloads/

An asset declares either a single `file` with `source`, `sha256` and `bytes`
(the original single-file .glb form) or a `files` list of {path, url, sha256,
bytes} for a glTF that ships as a document, a buffer and a folder of images.
A surface declares a `folder`, a `files` map of role -> filename and a
`sources` map of role -> {url, sha256, bytes}. An asset may also carry a
`derived` list of {name, file}: files a documented tool made from the fetched
ones, compiled under their own names; with `compile: false` the asset's own
`file` is not compiled, only its derived files are.
"""

from __future__ import annotations

import json
import pathlib
import sys


def load(root: pathlib.Path) -> dict:
    return json.loads((root / "assets" / "external" / "manifest.json").read_text())


def model_files(asset: dict):
    """Yield (path, url, sha256, bytes) for one model asset."""
    if "files" in asset:
        for entry in asset["files"]:
            yield entry["path"], entry["url"], entry["sha256"], entry["bytes"]
    else:
        yield asset["file"], asset["source"], asset["sha256"], asset["bytes"]


def surface_files(surface: dict):
    folder = surface["folder"]
    for role, filename in surface["files"].items():
        source = surface["sources"][role]
        yield f"{folder}/{filename}", source["url"], source["sha256"], source["bytes"]


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest = load(root)
    command = sys.argv[1] if len(sys.argv) > 1 else ""

    if command == "models":
        for asset in manifest.get("assets", []):
            if asset.get("compile", True):
                print(f"{asset['file']}={asset['name']}")
            # Files built from the fetched ones by a tool -- the levels of
            # detail cut from a tree in Blender -- compiled under their own
            # names.
            for derived in asset.get("derived", []):
                print(f"{derived['file']}={derived['name']}")
        return 0

    if command == "fetch-list":
        for asset in manifest.get("assets", []):
            for path, url, digest, size in model_files(asset):
                print("\t".join([asset["name"], url, path, digest, str(size)]))
        for surface in manifest.get("surfaces", []):
            for path, url, digest, size in surface_files(surface):
                print("\t".join([surface["name"], url, path, digest, str(size)]))
        return 0

    if command == "declared":
        for asset in manifest.get("assets", []):
            for path, *_ in model_files(asset):
                print(path)
            for derived in asset.get("derived", []):
                print(derived["file"])
        for surface in manifest.get("surfaces", []):
            for path, *_ in surface_files(surface):
                print(path)
            print(f"{surface['folder']}/info.json")
        return 0

    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())

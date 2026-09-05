#!/usr/bin/env python3
"""Fetch a Poly Haven model into downloads/ and print its manifest entry.

    scripts/polyhaven-fetch.py [--res 1k] [--format gltf|blend] [--dry-run] ASSET...

For each asset id (the slug in https://polyhaven.com/a/<id>) this asks the
Poly Haven API which files make up the glTF at the requested resolution,
fetches the document, its buffer and its textures into
`assets/external/downloads/polyhaven/<id>/<res>/` -- the layout the existing
manifest entries use -- verifies each against the MD5 the API publishes,
and prints a manifest entry with every file's SHA-256 and byte count, the
author from the asset's info, and its dimensions in metres.

The entry is printed, not written: `role`, `transformations` and the rest
are a person's decision, and the manifest is the record of that decision.
Every Poly Haven asset is CC0-1.0, which is what the entry says.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import sys
import urllib.request

API = "https://api.polyhaven.com"
AGENT = "cna-street-fetch/1.0"


def get_json(url: str):
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def fetch(url: str, dest: pathlib.Path, md5: str | None) -> None:
    if dest.exists() and (md5 is None or hashlib.md5(dest.read_bytes()).hexdigest() == md5):
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=600) as response, open(dest, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    if md5 is not None and hashlib.md5(dest.read_bytes()).hexdigest() != md5:
        raise RuntimeError(f"{dest}: MD5 does not match what the API published")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", nargs="+")
    parser.add_argument("--res", default="1k")
    parser.add_argument("--format", default="gltf", choices=("gltf", "blend"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    downloads = root / "assets" / "external" / "downloads"
    today = dt.date.today().isoformat()
    entries = []

    for asset in args.assets:
        info = get_json(f"{API}/info/{asset}")
        files = get_json(f"{API}/files/{asset}")
        fmt = args.format
        if fmt not in files or args.res not in files[fmt]:
            print(f"{asset}: no {fmt} at {args.res}", file=sys.stderr)
            return 1
        entry = files[fmt][args.res][fmt]
        folder = f"polyhaven{'-blend' if fmt == 'blend' else ''}/{asset}/{args.res}"
        main_name = entry["url"].rsplit("/", 1)[-1]
        wanted = [(f"{folder}/{main_name}", entry["url"], entry.get("md5"), entry["size"])]
        for relative, sub in entry.get("include", {}).items():
            wanted.append((f"{folder}/{relative}", sub["url"], sub.get("md5"), sub["size"]))

        listed = []
        for path, url, md5, size in wanted:
            dest = downloads / path
            if not args.dry_run:
                fetch(url, dest, md5)
                digest = hashlib.sha256(dest.read_bytes()).hexdigest()
                actual = dest.stat().st_size
            else:
                digest, actual = "", size
            listed.append({"path": path, "url": url, "sha256": digest, "bytes": actual})
            print(f"  {asset}: {path} ({actual} bytes)", file=sys.stderr)

        authors = info.get("authors", {})
        author = ", ".join(authors.keys()) if isinstance(authors, dict) else str(authors)
        dims = info.get("dimensions") or [0, 0, 0]
        entries.append({
            "name": f"ph-{asset.lower().replace('_', '-')}",
            "title": info.get("name", asset),
            "author": author,
            "copyright": f"(c) Poly Haven / {author}",
            "source": f"https://polyhaven.com/a/{asset}",
            "sourceRepository": "https://polyhaven.com/models",
            "licence": "CC0-1.0",
            "licenceUrl": "https://creativecommons.org/publicdomain/zero/1.0/legalcode",
            "attributionRequired": False,
            "redistributionAllowed": True,
            "retrieved": today,
            "originalFormat": (
                f"glTF 2.0 (.gltf + .bin + JPEG/PNG textures at {args.res})"
                if fmt == "gltf" else f"Blender scene (.blend) with {args.res} textures"),
            "file": wanted[0][0],
            "files": listed,
            "sizeMetres": [round(d / 1000.0, 2) for d in dims],
            "transformations": [
                "compiled to .cnb by cna_tool_gltf_to_cnb during the content build",
                "placed at its authored size: the file is in metres",
            ],
            "role": "TODO",
        })

    json.dump(entries, sys.stdout, indent=1, ensure_ascii=False)
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())

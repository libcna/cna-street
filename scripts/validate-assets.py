#!/usr/bin/env python3
"""Check that every external asset is declared, licensed and intact.

This exists because "it downloaded" is not a licence, and because a manifest
that nobody checks is a manifest that drifts. It is run by hand, by CI, or by
`cmake --build build --target validate-assets`, and it fails loudly rather than
warning: an asset whose licence cannot be established is not a smaller problem
than an asset that fails to load.

What it checks, per asset:

  * every field the manifest promises is present and non-empty
  * the licence is one this repository can actually use
  * an attribution-required asset names an author and a copyright line
  * a redistribution-forbidden asset is not committed to the repository
  * the file, if fetched, matches its recorded SHA-256 and byte count

And across the manifest:

  * no two assets share a local name
  * no two assets share a source URL
  * every fetched file under downloads/ is declared
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys

# Licences this repository can use. Deliberately a short allow-list rather than
# a deny-list: a licence nobody has thought about is a licence nobody has
# checked, and the failure mode of guessing wrong is legal rather than visual.
ALLOWED = {
    "CC0-1.0": {"attribution": False, "redistribute": True},
    "CC-BY-4.0": {"attribution": True, "redistribute": True},
    "MIT": {"attribution": True, "redistribute": True},
    "Apache-2.0": {"attribution": True, "redistribute": True},
}

REQUIRED = (
    "name", "title", "author", "copyright", "source", "sourceRepository",
    "licence", "licenceUrl", "attributionRequired", "redistributionAllowed",
    "retrieved", "originalFormat", "file", "sha256", "bytes", "transformations",
    "role",
)


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest_path = root / "assets" / "external" / "manifest.json"
    downloads = root / "assets" / "external" / "downloads"

    if not manifest_path.is_file():
        print(f"validate-assets: no manifest at {manifest_path}", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text())
    assets = manifest.get("assets", [])
    if not assets:
        print("validate-assets: the manifest declares no assets", file=sys.stderr)
        return 1

    problems: list[str] = []
    seen_names: dict[str, str] = {}
    seen_sources: dict[str, str] = {}
    declared_files: set[str] = set()
    checked = 0

    for asset in assets:
        name = asset.get("name", "<unnamed>")

        for field in REQUIRED:
            if field not in asset or asset[field] in ("", None, []):
                problems.append(f"{name}: missing or empty field '{field}'")

        licence = asset.get("licence", "")
        rules = ALLOWED.get(licence)
        if rules is None:
            problems.append(
                f"{name}: licence '{licence}' is not on the allow-list. Add it there "
                f"only after establishing that it permits use and redistribution "
                f"under this repository's terms."
            )
        else:
            if rules["attribution"] and not asset.get("attributionRequired"):
                problems.append(
                    f"{name}: {licence} requires attribution but the manifest says it does not"
                )
            if not rules["redistribute"] and asset.get("redistributionAllowed"):
                problems.append(
                    f"{name}: {licence} does not permit redistribution but the manifest says it does"
                )

        if asset.get("attributionRequired"):
            for field in ("author", "copyright"):
                if not asset.get(field):
                    problems.append(
                        f"{name}: attribution is required but '{field}' is empty"
                    )

        if name in seen_names:
            problems.append(f"{name}: local name is already used by {seen_names[name]}")
        seen_names[name] = name

        source = asset.get("source", "")
        if source in seen_sources:
            problems.append(f"{name}: same source URL as {seen_sources[source]}")
        seen_sources[source] = name

        filename = asset.get("file", "")
        declared_files.add(filename)
        path = downloads / filename
        if not path.is_file():
            # Not an error. The files are fetched rather than committed, and a
            # tree that has not fetched them is a supported state.
            continue

        checked += 1
        size = path.stat().st_size
        if size != asset.get("bytes"):
            problems.append(
                f"{name}: {filename} is {size} bytes, the manifest says {asset.get('bytes')}"
            )
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != asset.get("sha256"):
            problems.append(
                f"{name}: {filename} hashes to {digest}, the manifest says {asset.get('sha256')}"
            )

        if not asset.get("redistributionAllowed"):
            # A file that may not be redistributed must not be in git.
            tracked = (root / ".git").is_dir()
            if tracked:
                problems.append(
                    f"{name}: redistribution is not allowed, so {filename} must never be committed"
                )

    if downloads.is_dir():
        for path in sorted(downloads.iterdir()):
            if path.is_file() and path.name not in declared_files:
                problems.append(
                    f"{path.name} is present in downloads/ but not declared in the manifest. "
                    f"Every file that reaches the content build has to have a licence."
                )

    print(
        f"validate-assets: {len(assets)} declared, {checked} present and verified, "
        f"{len(problems)} problem(s)"
    )
    for problem in problems:
        print(f"  {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

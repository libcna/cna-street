#!/usr/bin/env python3
"""Check that every external asset is declared, licensed and intact.

This exists because "it downloaded" is not a licence, and because a manifest
that nobody checks is a manifest that drifts. It is run by hand, by CI, or by
`cmake --build build --target validate-assets`, and it fails loudly rather than
warning: an asset whose licence cannot be established is not a smaller problem
than an asset that fails to load.

What it checks, per model asset and per scanned surface:

  * every field the manifest promises is present and non-empty
  * the licence is one this repository can actually use
  * an attribution-required asset names an author and a copyright line
  * a redistribution-forbidden asset is not committed to the repository
  * every file, if fetched, matches its recorded SHA-256 and byte count

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

# Licences a *tool* may carry. A tool is run, never linked, never
# redistributed and never drawn -- Blender itself is GPL and nothing it
# exports is -- so copyleft is fine here and would not be for an asset.
TOOL_ALLOWED = {"GPL-3.0-or-later", "GPL-2.0-or-later", "MIT", "Apache-2.0", "BSD-3-Clause"}
TOOL_REQUIRED = ("name", "title", "author", "source", "sourceRepository", "licence",
                 "licenceUrl", "retrieved", "file", "sha256", "bytes", "role")

COMMON = (
    "name", "title", "author", "copyright", "source", "sourceRepository",
    "licence", "licenceUrl", "attributionRequired", "redistributionAllowed",
    "retrieved", "originalFormat", "transformations", "role",
)
MODEL_REQUIRED = COMMON + ("file",)
SURFACE_REQUIRED = COMMON + ("folder", "files", "sources", "physicalMetres", "tileMetres")

# Files under downloads/ that are never declared: fetch scaffolding and the
# per-asset metadata copies the fetch script keeps for the attribution table.
IGNORED_SUFFIXES = (".log", ".part", ".partial")
IGNORED_NAMES = {"info.json"}


def files_of_model(asset: dict):
    """(path, sha256, bytes) for every file a model asset declares."""
    if "files" in asset:
        for entry in asset["files"]:
            yield entry["path"], entry.get("sha256"), entry.get("bytes")
    else:
        yield asset.get("file", ""), asset.get("sha256"), asset.get("bytes")


def files_of_surface(surface: dict):
    folder = surface.get("folder", "")
    for role, filename in surface.get("files", {}).items():
        source = surface.get("sources", {}).get(role, {})
        yield f"{folder}/{filename}", source.get("sha256"), source.get("bytes")


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest_path = root / "assets" / "external" / "manifest.json"
    downloads = root / "assets" / "external" / "downloads"

    if not manifest_path.is_file():
        print(f"validate-assets: no manifest at {manifest_path}", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text())
    assets = manifest.get("assets", [])
    surfaces = manifest.get("surfaces", [])
    tools = manifest.get("tools", [])
    if not assets and not surfaces:
        print("validate-assets: the manifest declares nothing", file=sys.stderr)
        return 1

    problems: list[str] = []
    seen_names: dict[str, str] = {}
    seen_sources: dict[str, str] = {}
    declared_files: set[str] = set()
    checked = 0

    def check_common(entry: dict, required: tuple[str, ...], kind: str) -> str:
        name = entry.get("name", "<unnamed>")
        for field in required:
            if field not in entry or entry[field] in ("", None, [], {}):
                problems.append(f"{name}: missing or empty field '{field}'")

        licence = entry.get("licence", "")
        rules = ALLOWED.get(licence)
        if rules is None:
            problems.append(
                f"{name}: licence '{licence}' is not on the allow-list. Add it there "
                f"only after establishing that it permits use and redistribution "
                f"under this repository's terms."
            )
        else:
            if rules["attribution"] and not entry.get("attributionRequired"):
                problems.append(
                    f"{name}: {licence} requires attribution but the manifest says it does not"
                )
            if not rules["redistribute"] and entry.get("redistributionAllowed"):
                problems.append(
                    f"{name}: {licence} does not permit redistribution but the manifest says it does"
                )

        if entry.get("attributionRequired"):
            for field in ("author", "copyright"):
                if not entry.get(field):
                    problems.append(f"{name}: attribution is required but '{field}' is empty")

        if name in seen_names:
            problems.append(f"{name}: local name is already used by a {seen_names[name]}")
        seen_names[name] = kind

        source = entry.get("source", "")
        if source in seen_sources:
            problems.append(f"{name}: same source URL as {seen_sources[source]}")
        seen_sources[source] = name
        return name

    def check_file(name: str, path: str, digest: str | None, size: int | None,
                   redistributable: bool) -> None:
        nonlocal checked
        declared_files.add(path)
        full = downloads / path
        if not full.is_file():
            # Not an error. The files are fetched rather than committed, and a
            # tree that has not fetched them is a supported state.
            return
        checked += 1
        actual = full.stat().st_size
        if size is not None and actual != size:
            problems.append(f"{name}: {path} is {actual} bytes, the manifest says {size}")
        actual_digest = hashlib.sha256(full.read_bytes()).hexdigest()
        if digest is not None and actual_digest != digest:
            problems.append(
                f"{name}: {path} hashes to {actual_digest}, the manifest says {digest}"
            )
        if not redistributable and (root / ".git").is_dir():
            # A file that may not be redistributed must not be in git.
            problems.append(
                f"{name}: redistribution is not allowed, so {path} must never be committed"
            )

    for asset in assets:
        name = check_common(asset, MODEL_REQUIRED, "model")
        if "files" not in asset:
            for field in ("sha256", "bytes"):
                if not asset.get(field):
                    problems.append(f"{name}: missing or empty field '{field}'")
        for path, digest, size in files_of_model(asset):
            check_file(name, path, digest, size, bool(asset.get("redistributionAllowed")))
        # Something built from the fetched files by a tool -- a level of detail
        # cut in Blender -- is declared so the scan of downloads/ knows it, and
        # so the manifest says what was done to make it.
        for derived in asset.get("derived", []):
            if not derived.get("name") or not derived.get("file"):
                problems.append(f"{name}: a derived entry needs a 'name' and a 'file'")
                continue
            declared_files.add(derived["file"])
            if derived["name"] in seen_names and derived["name"] != name:
                problems.append(f"{name}: derived name '{derived['name']}' is already used")
            seen_names[derived["name"]] = "derived"

    for surface in surfaces:
        name = check_common(surface, SURFACE_REQUIRED, "surface")
        roles = set(surface.get("files", {}).keys())
        for required in ("albedo", "normal"):
            if required not in roles:
                problems.append(f"{name}: a surface needs an '{required}' map")
        if "orm" not in roles and "roughness" not in roles:
            problems.append(f"{name}: a surface needs an 'orm' or a 'roughness' map")
        missing = roles - set(surface.get("sources", {}).keys())
        if missing:
            problems.append(f"{name}: no source recorded for {sorted(missing)}")
        for path, digest, size in files_of_surface(surface):
            check_file(name, path, digest, size, bool(surface.get("redistributionAllowed")))
        declared_files.add(f"{surface.get('folder', '')}/info.json")

    for tool in tools:
        name = tool.get("name", "<unnamed tool>")
        for field in TOOL_REQUIRED:
            if field not in tool or tool[field] in ("", None):
                problems.append(f"{name}: missing or empty field '{field}'")
        if tool.get("licence") not in TOOL_ALLOWED:
            problems.append(f"{name}: tool licence '{tool.get('licence')}' is not on the tool allow-list")
        if name in seen_names:
            problems.append(f"{name}: local name is already used by a {seen_names[name]}")
        seen_names[name] = "tool"
        check_file(name, tool.get("file", ""), tool.get("sha256"), tool.get("bytes"), True)

    if downloads.is_dir():
        for path in sorted(downloads.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(downloads).as_posix()
            if path.name in IGNORED_NAMES or path.suffix in IGNORED_SUFFIXES:
                continue
            if relative not in declared_files:
                problems.append(
                    f"{relative} is present in downloads/ but not declared in the manifest. "
                    f"Every file that reaches the content build has to have a licence."
                )

    print(
        f"validate-assets: {len(assets)} model(s), {len(surfaces)} surface(s) and "
        f"{len(tools)} tool(s) declared, "
        f"{checked} file(s) present and verified, {len(problems)} problem(s)"
    )
    for problem in problems:
        print(f"  - {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

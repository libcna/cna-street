#!/usr/bin/env python3
"""Print the attribution tables for assets/ATTRIBUTION.md from the manifest.

The tables in that file are pasted from this script's output rather than typed,
so the attribution and the manifest cannot drift apart. Run it after changing
the manifest and replace the tables.
"""

from __future__ import annotations

import json
import pathlib
from collections import defaultdict


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest = json.loads((root / "assets" / "external" / "manifest.json").read_text())
    assets = manifest.get("assets", [])
    surfaces = manifest.get("surfaces", [])

    def by_author(entries):
        table = defaultdict(list)
        for entry in entries:
            table[entry["author"]].append(entry["title"])
        return table

    def licence_lists(entries):
        by = defaultdict(list)
        for entry in entries:
            by[entry["licence"]].append(entry["title"])
        return by

    for heading, entries in (("Models", assets), ("Surfaces", surfaces)):
        print(f"### {heading}\n")
        counts = defaultdict(int)
        for entry in entries:
            counts[entry["sourceRepository"]] += 1
        print("| Source | Count |\n| --- | --- |")
        for source, count in sorted(counts.items()):
            print(f"| {source} | {count} |")
        print("\n| Author | Titles |\n| --- | --- |")
        for author, titles in sorted(by_author(entries).items()):
            print(f"| {author} | {', '.join(sorted(titles))} |")
        print()
        for licence, titles in sorted(licence_lists(entries).items()):
            print(f"{licence}: {', '.join(sorted(titles))}.\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env bash
# Fetches the external assets listed in assets/external/manifest.json -- the
# glTF models and the scanned PBR surfaces -- and verifies each file against
# its recorded SHA-256.
#
# They are fetched rather than committed for the same reason the framework
# checkouts are: they are large, they belong to somebody else, and the manifest
# is the part of them that is this project's -- what was taken, from where,
# under what licence, and what was done to it. A tree without them still builds
# and still runs; it has generated surfaces and generated props where the
# scanned and imported ones would be.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/assets/external/manifest.json"
target="${1:-$root/assets/external/downloads}"

if [[ ! -f "$manifest" ]]; then
    echo "no manifest at $manifest" >&2
    exit 1
fi

command -v python3 >/dev/null || { echo "python3 is needed to read the manifest" >&2; exit 1; }
command -v curl    >/dev/null || { echo "curl is needed to fetch" >&2; exit 1; }
command -v sha256sum >/dev/null || { echo "sha256sum is needed to verify" >&2; exit 1; }

mkdir -p "$target"

# name<TAB>url<TAB>path<TAB>sha256<TAB>bytes, one file per line; an asset that
# ships as a document, a buffer and a folder of images is several lines.
readarray -t rows < <(python3 "$root/scripts/manifest-tool.py" fetch-list)

ok=0; skipped=0; failed=0
for row in "${rows[@]}"; do
    IFS=$'\t' read -r name url path want size <<<"$row"
    dest="$target/$path"
    mkdir -p "$(dirname "$dest")"

    if [[ -f "$dest" ]]; then
        have="$(sha256sum "$dest" | cut -d' ' -f1)"
        if [[ "$have" == "$want" ]]; then
            skipped=$((skipped + 1))
            continue
        fi
        printf '  %-26s %s: checksum mismatch, refetching\n' "$name" "$(basename "$path")"
    fi

    # A browser-like agent string: Poly Haven's file host refuses the default
    # curl and urllib agents with a 403, and the download is public either way.
    if ! curl -fsSL --retry 3 --retry-delay 2 -A "cna-street-fetch/1.0" -o "$dest.partial" "$url"; then
        printf '  %-26s %s: FETCH FAILED\n' "$name" "$(basename "$path")" >&2
        rm -f "$dest.partial"
        failed=$((failed + 1))
        continue
    fi

    have="$(sha256sum "$dest.partial" | cut -d' ' -f1)"
    if [[ "$have" != "$want" ]]; then
        printf '  %-26s %s: CHECKSUM MISMATCH (got %s)\n' "$name" "$(basename "$path")" "$have" >&2
        rm -f "$dest.partial"
        failed=$((failed + 1))
        continue
    fi
    mv "$dest.partial" "$dest"
    printf '  %-26s %s fetched\n' "$name" "$(basename "$path")"
    ok=$((ok + 1))
done

# Poly Haven publishes each asset's metadata beside its files; a copy is kept
# with the surfaces so the licence check can name the author without the
# network. Fetched once, never verified: it is documentation, not content.
python3 - "$manifest" "$target" <<'PY'
import json, pathlib, sys, urllib.request
manifest = json.load(open(sys.argv[1])); target = pathlib.Path(sys.argv[2])
opener = urllib.request.build_opener(); opener.addheaders = [("User-Agent", "cna-street-fetch/1.0")]
for surface in manifest.get("surfaces", []):
    info = target / surface["folder"] / "info.json"
    if info.exists() or "polyhaven.com/a/" not in surface.get("source", ""):
        continue
    slug = surface["source"].rsplit("/", 1)[-1]
    try:
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_bytes(opener.open(f"https://api.polyhaven.com/info/{slug}").read())
    except Exception as failure:  # noqa: BLE001 -- documentation only
        print(f"  {surface['name']:<26} info.json not fetched: {failure}")
PY

echo "assets: $ok fetched, $skipped already present, $failed failed -> $target"
[[ $failed -eq 0 ]]

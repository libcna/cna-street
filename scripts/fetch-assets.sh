#!/usr/bin/env bash
# Fetches the external glTF assets listed in assets/external/manifest.json and
# verifies each against its recorded SHA-256.
#
# They are fetched rather than committed for the same reason the framework
# checkouts are: they are large (about 110 MB of 4K-textured product-shot
# assets), they belong to somebody else, and the manifest is the part of them
# that is this project's -- what was taken, from where, under what licence, and
# what was done to it. A tree without them still builds and still runs; it just
# has procedural props where the imported ones would be.
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

# name<TAB>url<TAB>file<TAB>sha256, one asset per line.
readarray -t rows < <(python3 - "$manifest" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
for a in doc["assets"]:
    print("\t".join([a["name"], a["source"], a["file"], a["sha256"]]))
PY
)

ok=0; skipped=0; failed=0
for row in "${rows[@]}"; do
    IFS=$'\t' read -r name url file want <<<"$row"
    dest="$target/$file"

    if [[ -f "$dest" ]]; then
        have="$(sha256sum "$dest" | cut -d' ' -f1)"
        if [[ "$have" == "$want" ]]; then
            printf '  %-26s already present\n' "$name"
            skipped=$((skipped + 1))
            continue
        fi
        printf '  %-26s checksum mismatch, refetching\n' "$name"
    fi

    if ! curl -fsSL --retry 3 --retry-delay 2 -o "$dest.partial" "$url"; then
        printf '  %-26s FETCH FAILED\n' "$name" >&2
        rm -f "$dest.partial"
        failed=$((failed + 1))
        continue
    fi

    have="$(sha256sum "$dest.partial" | cut -d' ' -f1)"
    if [[ "$have" != "$want" ]]; then
        printf '  %-26s CHECKSUM MISMATCH (got %s)\n' "$name" "$have" >&2
        rm -f "$dest.partial"
        failed=$((failed + 1))
        continue
    fi
    mv "$dest.partial" "$dest"
    printf '  %-26s fetched\n' "$name"
    ok=$((ok + 1))
done

echo "assets: $ok fetched, $skipped already present, $failed failed -> $target"
[[ $failed -eq 0 ]]

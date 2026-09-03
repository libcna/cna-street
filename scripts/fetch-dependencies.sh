#!/usr/bin/env bash
# Clone (or update) the sibling checkouts cna-street needs.
#
# CNA is consumed with add_subdirectory() from a sibling directory, and CNA
# itself resolves sharp-runtime / easy-gl / meta-gl relative to its own root.
# The resulting layout is:
#
#   <parent>/cna-street   <- this repository
#   <parent>/cna
#   <parent>/sharp-runtime
#   <parent>/easy-gl      (GL renderer family only)
#   <parent>/meta-gl      (easy-gl's own sibling)
#
# Usage:
#   scripts/fetch-dependencies.sh            # branch tips (next / develop)
#   scripts/fetch-dependencies.sh --pinned   # the SHAs in dependencies.lock
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
parent="$(dirname "$here")"
lock="$here/dependencies.lock"
pinned=0
[ "${1:-}" = "--pinned" ] && pinned=1

lock_sha() { sed -n "s/^$1=//p" "$lock" | head -1; }

fetch() {                       # fetch <dir> <url> <branch> <lock-key>
    local dir="$parent/$1" url="$2" branch="$3" key="$4" sha
    if [ ! -d "$dir/.git" ]; then
        echo ":: cloning $1 ($branch)"
        git clone --branch "$branch" "$url" "$dir"
    else
        echo ":: updating $1"
        git -C "$dir" fetch origin "$branch" --tags
    fi
    if [ "$pinned" = 1 ]; then
        sha="$(lock_sha "$key")"
        echo ":: pinning $1 -> $sha"
        git -C "$dir" checkout --quiet "$sha"
    else
        git -C "$dir" checkout --quiet "$branch"
        git -C "$dir" merge --quiet --ff-only "origin/$branch" || true
    fi
    echo "   $1 = $(git -C "$dir" rev-parse HEAD)"
}

fetch cna           https://github.com/openeggbert/cna.git           next    CNA
fetch sharp-runtime https://github.com/openeggbert/sharp-runtime.git next    SHARP_RUNTIME
fetch easy-gl       https://github.com/openeggbert/easy-gl.git       develop EASY_GL
fetch meta-gl       https://github.com/openeggbert/meta-gl.git       develop META_GL

echo ":: initialising CNA submodules (SDL, SDL_image, SDL_mixer, cgltf, stb, enet)"
git -C "$parent/cna" submodule update --init --recursive --depth 1

echo
echo "All dependencies are in $parent."

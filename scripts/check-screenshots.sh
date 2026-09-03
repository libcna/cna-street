#!/usr/bin/env bash
# Renders every named viewpoint and compares it with the committed set.
#
# The scene comes from a seed and the viewpoints are fixed, so this is a real
# regression test: change a generator or a shader and the difference shows up in
# a known image instead of being noticed six commits later.
#
# It is tolerant on purpose. A driver update, a Mesa version or an
# anti-aliasing decision moves individual pixels without changing the picture,
# and a test that fails on those is a test somebody turns off. The default
# allows 2 % of pixels to differ by more than 8/255.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$here/build}"
reference="${REFERENCE_DIR:-$here/docs/screenshots}"
tolerance="${TOLERANCE:-0.02}"
width="${WIDTH:-1024}"
height="${HEIGHT:-576}"

street="$build/bin/cna-street"
compare="$build/bin/compare-images"
for tool in "$street" "$compare"; do
    if [[ ! -x "$tool" ]]; then
        echo "check-screenshots: $tool is not built; run cmake --build $build" >&2
        exit 2
    fi
done

candidates="$(mktemp -d)"
trap 'rm -rf "$candidates"' EXIT

run() {
    # A display is needed even for an offscreen capture: the renderer creates a
    # real GL context. xvfb-run supplies one where there is no desktop.
    if [[ -n "${DISPLAY:-}" ]]; then
        "$street" "$@"
    elif command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a -s "-screen 0 1400x800x24" "$street" "$@"
    else
        echo "check-screenshots: no DISPLAY and no xvfb-run" >&2
        exit 2
    fi
}

echo "check-screenshots: rendering ${width}x${height} into $candidates"
run --capture "$candidates" --width "$width" --height "$height" --no-overlay >/dev/null

status=0
count=0
for expected in "$reference"/*.png; do
    name="$(basename "$expected")"
    actual="$candidates/$name"
    if [[ ! -f "$actual" ]]; then
        echo "MISS $name: the capture did not produce it"
        status=1
        continue
    fi
    "$compare" "$expected" "$actual" --tolerance "$tolerance" || status=1
    count=$((count + 1))
done

echo "check-screenshots: compared $count viewpoints"
exit "$status"

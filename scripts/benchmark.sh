#!/usr/bin/env bash
# Runs every benchmark preset, one process at a time, and appends each result
# to one file, so a build can be compared with another by two files and a
# diff. Usage:
#
#   scripts/benchmark.sh [output] [-- extra cna-street args]
#
# The output defaults to build/benchmarks/<git revision>-<date>.csv; give a
# .json path for one JSON object per line instead. Every run is at 1600x900
# with the shipped settings unless the extra arguments say otherwise, and the
# environment's BUILD_DIR, PRESETS and RUNS override the build tree, the
# preset list and how many times each preset is run (default 1).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$here/build}"
street="$build/bin/cna-street"
if [[ ! -x "$street" ]]; then
    echo "benchmark: $street is not built; run cmake --build $build" >&2
    exit 2
fi

output=""
if [[ $# -gt 0 && "$1" != "--" ]]; then output="$1"; shift; fi
[[ $# -gt 0 && "$1" == "--" ]] && shift
if [[ -z "$output" ]]; then
    revision="$(git -C "$here" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    output="$build/benchmarks/${revision}-$(date +%Y%m%d-%H%M).csv"
fi
mkdir -p "$(dirname "$output")"

presets="${PRESETS:-$("$street" --benchmark-list | awk '{print $1}')}"
runs="${RUNS:-1}"

# The GPU's own name, for the result's `gpu` column: the device can only
# name its display adapter. From glxinfo where there is one, else whatever
# the caller exported.
if [[ -z "${CNA_STREET_GPU:-}" ]] && command -v glxinfo >/dev/null 2>&1; then
    CNA_STREET_GPU="$(glxinfo -B 2>/dev/null | sed -n 's/^OpenGL renderer string: //p' | head -1)"
fi
export CNA_STREET_GPU="${CNA_STREET_GPU:-}"

echo "benchmark: $(git -C "$here" rev-parse --short HEAD 2>/dev/null || echo unknown) -> $output"
echo "benchmark: load average $(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null || echo n/a)"
for preset in $presets; do
    for run in $(seq 1 "$runs"); do
        line="$("$street" --benchmark "$preset" --benchmark-output "$output" \
                  --width 1600 --height 900 "$@" 2>/dev/null | grep '^{' | tail -1)"
        if [[ -z "$line" ]]; then
            echo "benchmark: $preset produced no result" >&2
            continue
        fi
        # The headline, for the terminal; the file has the rest.
        python3 - "$preset" "$line" <<'EOF' 2>/dev/null || echo "$preset: $line"
import json, sys
r = json.loads(sys.argv[2])
print(f"{sys.argv[1]:<9} cpu {r['cpuMeanMs']:6.2f} ms  gpu {r['gpuFrameMs']:6.2f} ms  "
      f"shadow {r['gpuShadowMs']:5.2f}  opaque {r['gpuOpaqueMs']:5.2f}  post {r['gpuPostMs']:5.2f}  "
      f"draws {r['draws']:6.0f}  shadow draws {r['shadowDraws']:6.0f}  tris {r['triangles']/1e6:5.2f} M  "
      f"load {r['loadAverage']:.1f}")
EOF
    done
done
echo "benchmark: done -> $output"

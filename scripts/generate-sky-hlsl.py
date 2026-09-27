#!/usr/bin/env python3
"""Generate the Direct3D sky variants from the existing Vulkan GLSL stages."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "street/src/Render/shaders/sky"
OUTPUT = SHADERS / "SkyHlsl.generated.hpp"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cna-root", type=Path, required=True)
    parser.add_argument("--glslang", type=Path, required=True)
    parser.add_argument("--spirv-cross", type=Path, required=True)
    args = parser.parse_args()

    sys.path.insert(0, str(args.cna_root / "tools/shader_package"))
    from generate_post_process_hlsl import translate

    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Rebuild with scripts/generate-sky-hlsl.py.",
        "#pragma once",
        "#include <string_view>",
        "namespace CnaStreet::SkyShaders {",
    ]
    with tempfile.TemporaryDirectory(prefix="cna-street-sky-hlsl-") as temp:
        for stage, symbol in (("vert", "kDirectXVertexHlsl"),
                              ("frag", "kDirectXFragmentHlsl")):
            source = SHADERS / f"sky.vulkan.{stage}.glsl"
            hlsl = translate(source, args.glslang, args.spirv_cross, None,
                             Path(temp), first_fragment_cbuffer_slot=4)
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            lines.append(f"// {source.name} SHA-256: {digest}")
            lines.append(f'inline constexpr std::string_view {symbol} = '
                         f'R"CNA_HLSL({hlsl})CNA_HLSL";')
    lines.append("}")
    with OUTPUT.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
    print(f"Wrote {OUTPUT}")


if __name__ == "__main__":
    main()

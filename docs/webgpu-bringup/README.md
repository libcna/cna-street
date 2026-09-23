# cna-street on WebGPU (2026-09-23)

The street runs on CNA's WebGPU renderer (wgpu-native v29.0.1.1, AMD Radeon 780M / RADV, Mesa
25.0.7), compared against the OPENGL33 (EasyGL) baseline and the Vulkan renderer the previous pass
brought up. Three defects were found and fixed: one in this project, two in CNA's WebGPU renderer.
The street now renders the same picture on all three.

## What was broken

| Where | What | Fix |
|---|---|---|
| CNA `STREETW-0001` | Every instanced draw took the position-only `instanced3d` program whatever effect was applied, so every tree, parked car, bench and piece of street furniture was a flat white silhouette | PbrEffect keeps its family when the draw is instanced |
| cna-street `STREETW-0002` | No sky at all, and `the sky shader did not compile: ... found "#"` at start-up | The sky package gained a WGSL variant, and the selection asks which *language* a renderer runs rather than whether it runs source at all |
| CNA `STREETW-0003` | The whole frame sat half a pixel up and left of the same scene on the other two renderers | The WebGPU renderer had no XNA pixel-centre correction at all; it now carries Vulkan's, field for field |

All three are in CNA's `plans/plan_street_webgpu.md`.

`webgpu-before-fixes/` holds three of the viewpoints as they were: white props, black sky.

## How far the three renderers now agree

`compare-images` over the 18 named viewpoints, fraction of pixels differing by more than 8/255 in
any channel, against the OPENGL33 capture:

| | Vulkan | WebGPU |
|---|---|---|
| as found | 0.3 - 1.9 % | 52 - 98 % |
| after STREETW-0001 and 0002 | 0.3 - 1.9 % | 8 - 32 % |
| after STREETW-0003 | 0.3 - 1.9 % | **0.5 - 8.8 %**, 17 of the 18 under 3.2 % |

The half-pixel offset was found by measurement rather than by reading code, and the order is worth
keeping because every earlier candidate was ruled out first:

* **Not noise.** Each renderer is deterministic: two captures of the same renderer differ by 0.00 %.
* **Not tone.** Region means agreed to within 1-5/255, and the difference was not monotonic in
  brightness, so not a gamma, exposure or tone-map difference.
* **Not sharpness.** The high-frequency detail energy of the same oblique facades, road and canopy
  matched to three decimal places, so not filtering, anisotropy or mip selection.
* **Not anti-aliasing.** `--preset medium` turns MSAA off in all three and the gap grew slightly.
* **Not a pass.** With every optional pass off it was still 26 %, so it was in the base opaque pass.
* **A sub-pixel offset.** A search over sub-pixel shifts found the WebGPU frame displaced by half a
  pixel in both axes. After the fix that same search puts its optimum at exactly (0, 0).

The remaining few per cent are ordinary edge and sampling differences between two APIs on one GPU.
`06-above-the-junction`, the aerial view, is the one outlier at 8.8 %; it is the viewpoint with the
most distant geometry, and Vulkan is its worst case too (1.9 %).

## Build (one tree, three renderers)

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
      -DCNA_GRAPHICS_RENDERERS="OPENGL33;VULKAN;WEBGPU" \
      -DCNA_WEBGPU_ROOT=$HOME/deps/wgpu-native-v29.0.1.1 \
      -DCNA_WEBGPU_COMPILED_EFFECTS=ON        # the default renderer stays OPENGL33
cmake --build build -j8
```

## Run (never on the live desktop — CNA's private compositor)

```sh
R=../cna/tools/platform/run_gpu_tests_private.sh
CNA_GRAPHICS_RENDERER=WEBGPU $R --exec ./build/bin/cna-street --no-audio --capture out/webgpu --no-overlay
./build/bin/compare-images docs/webgpu-bringup/webgpu/07-the-long-view-south.jpg out/webgpu/07-the-long-view-south.png
```

## Regenerating the sky's shader payloads

The WGSL and the SPIR-V both come from `street/src/Render/shaders/sky/sky.vulkan.*.glsl`, so they
cannot drift apart. After editing either source:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 ../cna/tools/shader_package/generate_shader_package.py \
  street/src/Render/shaders/sky/package.json \
  --output street/src/Render/shaders/sky/SkyShaderPackage.generated.hpp \
  --naga $HOME/deps/naga-cli-28.0.0/bin/naga
```

naga 28.0.0 rather than 29.x: 29 needs rustc 1.87 and this machine has 1.85.

## Folders

* `webgpu/` — the 18 viewpoints from the fixed build
* `webgpu-before-fixes/` — three of them as found: white instanced props, black sky
* `side-by-side/` — OPENGL33 left, WebGPU right, at half size

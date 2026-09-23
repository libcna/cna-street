# cna-street on WebGPU (2026-09-23)

The street runs on CNA's WebGPU renderer (wgpu-native v29.0.1.1, AMD Radeon 780M / RADV, Mesa
25.0.7), compared against the OPENGL33 (EasyGL) baseline and the Vulkan renderer the previous pass
brought up. Two defects were found and fixed; one measured difference remains and belongs to CNA's
WebGPU renderer rather than to this project.

## What was broken

| Where | What | Fix |
|---|---|---|
| CNA `STREETW-0001` | Every instanced draw took the position-only `instanced3d` program whatever effect was applied, so every tree, parked car, bench and piece of street furniture was a flat white silhouette | PbrEffect keeps its family when the draw is instanced (`plans/plan_street_webgpu.md`) |
| cna-street `STREETW-0002` | No sky at all, and `the sky shader did not compile: ... found "#"` at start-up | The sky package gained a WGSL variant, and the selection asks which *language* a renderer runs rather than whether it runs source at all |

`webgpu-before-fixes/` holds three of the viewpoints as they were: white props, black sky.

## How far the three renderers now agree

`compare-images` over the 18 named viewpoints, fraction of pixels differing by more than 8/255 in
any channel, against the OPENGL33 capture:

| | Vulkan | WebGPU |
|---|---|---|
| before the fixes | 0.3 – 1.9 % | 52 – 98 % |
| after | 0.3 – 1.9 % | 8 – 32 % |

The remaining WebGPU difference is **not** a shading difference. Every renderer is deterministic
run to run (two captures of the same renderer differ by 0.00 %), region means agree to within
1–5/255, and the high-frequency detail energy of the same surfaces matches to three decimal places.
What differs is *where* the detail sits: a sub-pixel search over the two frames finds the WebGPU
image offset by about **half a pixel in both x and y** — aligning it drops the mean absolute
difference from 4.64 to 3.32/255. That is CNA's long-standing WebGPU pixel-centre-convention gap,
the one `WebGPU_PointSamplingContract` and `WebGPU_DescriptorCapacityContract` have recorded for
months (`plans/plan_webgpu.md`); it is measured here in a real scene rather than fixed, because
changing the convention moves every draw on that renderer.

Turning MSAA off in all three (`--preset medium`) does not close the gap, so it is not an
anti-aliasing difference either; disabling every optional pass
(`--no-bloom --no-ssao --no-fog --no-shadows --no-ibl --no-light-shafts --no-probes`) leaves it at
26 %, so it is in the base opaque pass, exactly as a raster-position offset would be.

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

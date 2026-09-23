# cna-street on SDL_GPU (2026-09-23)

The street runs on CNA's SDL_GPU renderer (SDL 3's GPU API, on its Vulkan backend here: RADV,
AMD Radeon 780M), compared against CNA's own Vulkan renderer on the same GPU and driver. Four
defects were found, all of them in CNA; nothing in this project needed changing. The street now
renders **the same picture** on both renderers.

The CNA side is `plans/plan_street_sdlgpu.md`, `STREETS-0001`..`STREETS-0004`.

## What was broken

| CNA task | What the street showed | Cause |
|---|---|---|
| `STREETS-0001` | Every tree, parked car, bench, chair and planter a flat white silhouette (`as-found/`) | An instanced draw always took SDL_GPU's position-only `instanced3d` program, whatever effect was applied -- the defect WebGPU had in `STREETW-0001` |
| `STREETS-0002` | Cold, blue-grey facades in shade, shop windows without reflections (`after-instancing/`), and `renderer has no image based lighting` at start-up | SDL_GPU's PBR shader had no image-based lighting; the street took its documented hemisphere fallback |
| `STREETS-0003` | FXAA and bloom visibly weaker than on Vulkan | SDL_GPU reported half-float textures as unfilterable, so the engine layer read the HDR scene with point sampling |
| `STREETS-0004` | SSAO different, worst from above the junction | A `ShaderEffect`'s texture units all used the SpriteBatch's sampler, so SSAO's tiled rotation noise was clamped instead of wrapped |

## How far the two renderers agree

`compare-images` over the 18 named viewpoints, fraction of pixels differing by more than 8/255 in
any channel, against the Vulkan capture:

| | pixels differing | mean difference |
|---|---|---|
| as found | 44 - 99 % | 15 - 80 /255 |
| after STREETS-0001 | 41 - 99 % | 7 - 22 /255 |
| after STREETS-0002 | 2.0 - 21.4 % | 0.5 - 4.8 /255 |
| after STREETS-0003 | 1.0 - 18.9 % | 0.3 - 4.1 /255 |
| after STREETS-0004 | **0.000 - 0.005 %** | **0.00 /255** |

The last two were found by measurement, not by reading code: no sub-pixel shift, frame means equal
to 0.3/255, but the SDL_GPU frame carried 12-20 % more high-frequency energy -- sharper, so less
filtering. Turning FXAA off in both made the energy match (STREETS-0003). What remained was then
bisected over the optional passes: with every pass off the two captures were already identical, and
with only SSAO off as well (STREETS-0004).

`side-by-side/` holds four viewpoints as they are now, Vulkan above and SDL_GPU below.

## Build (one tree, four renderers)

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
      -DCNA_GRAPHICS_RENDERERS="OPENGL33;VULKAN;WEBGPU;SDL_GPU" \
      -DCNA_WEBGPU_ROOT=$HOME/deps/wgpu-native-v29.0.1.1 \
      -DCNA_WEBGPU_COMPILED_EFFECTS=ON        # the default renderer stays OPENGL33
cmake --build build -j8 --target cna-street compare-images
```

SDL_GPU's `ShaderEffect` intake needs `libshaderc` at build time (found at configure).

## Run (never on the live desktop -- CNA's private compositor)

```sh
R=../cna/tools/platform/run_gpu_tests_private.sh
CNA_GRAPHICS_RENDERER=SDL_GPU $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/sdlgpu
CNA_GRAPHICS_RENDERER=VULKAN  $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/vulkan
./build/bin/compare-images out/vulkan/06-above-the-junction.png out/sdlgpu/06-above-the-junction.png
```

The one thing SDL_GPU still cannot give the street is GPU timing: SDL's GPU API has no timestamp
query, so the overlay's GPU times read "unavailable" there, as the street already reports.

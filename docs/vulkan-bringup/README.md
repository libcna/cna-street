# cna-street on EasyGL and on Vulkan (2026-09-22)

cna-street had only ever run on the EasyGL family, and had stopped working against CNA `next`
(29cfe0869). This pass restored the EasyGL baseline, then built and ran the street on CNA's Vulkan
renderer on the real GPU (AMD Radeon 780M, RADV, Mesa 25.0.7) and compared the two.

Result: every one of the 18 named viewpoints and 8 pedestrian close-ups differs between EasyGL
and Vulkan by a mean of 0.1–0.9 / 255 per channel (`side-by-side/`), with the Khronos validation
layer reporting no errors over 300 frames.

## What was broken, and where it was fixed

| Where | What | Fix |
|---|---|---|
| CNA STREET-0001 | Configuring CNA as a subproject failed (`cna_apply_test_display_policy_to Function invoked with incorrect arguments`) | `cmake/TestDisplayPolicy.cmake` |
| CNA STREET-0002 | No shadows at all: a High cascade atlas (6144/8192 px) exceeded XNA's HiDef ceiling of 4096 | engine-layer render targets may use the renderer's real limit |
| cna-street | Asphalt, paving and car atlases stretched down the street | SpriteBatch now publishes its LinearClamp into `SamplerStates[0]` (XNA/FNA behaviour); the 3D passes set their own `LinearWrap` |
| cna-street | Every reflection probe failed ("render target must be resolved") | read a probe face back after unsetting the target (XNA rule) |
| CNA STREET-0003 | Vulkan: first 3D draw threw (default white texture's descriptor set) | chaining allocator |
| CNA STREET-0004 | Vulkan: device lost -- per-frame uniform rings (512 PBR, 32 skinned) bound past their end | rings grow per frame |
| CNA STREET-0005 | Vulkan: 0.3 fps -- every caster draw allocated a VkBuffer | engine-matrix arena |
| cna-street | Vulkan: black sky (own GLSL shader, Vulkan runs SPIR-V only) | same sky as an offline-compiled SPIR-V `ShaderPackageEXT` (`street/src/Render/shaders/sky/`) |
| CNA STREET-0006 | Vulkan: ghost city over the scene -- prepass rendered mirrored, SSAO leaned on it | prepass Y flip + SSAO kernel Y |
| CNA STREET-0007 | Vulkan: shadows from the wrong faces -- caster winding mirrored | caster Y flip, receiver reads top-down |
| CNA STREET-0008 | Vulkan: tall buildings cast no shadow -- casters in front of the near plane clipped | GL depth range in the casters |

Details, root causes and regression tests: CNA `plans/plan_street.md` (branch `street`).

The pedestrians' faces are MakeHuman heads with correct eyes, brows, lips and hair on both
renderers (`easygl/face-*.jpg`, `vulkan/face-*.jpg`); nothing resembling the old "insect" heads.

## Build (one tree, both renderers)

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
      -DCNA_GRAPHICS_RENDERERS="OPENGL33;VULKAN"      # default stays OPENGL33
cmake --build build -j8
```

## Run (never on the live desktop -- CNA's private compositor)

```sh
R=../cna/tools/platform/run_gpu_tests_private.sh
# EasyGL
$R --exec ./build/bin/cna-street --no-audio --capture out/easygl --no-overlay
# Vulkan
CNA_GRAPHICS_RENDERER=VULKAN $R --exec ./build/bin/cna-street --no-audio --capture out/vulkan --no-overlay
# Vulkan with the validation layer
CNA_GRAPHICS_RENDERER=VULKAN VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    $R --exec ./build/bin/cna-street --no-audio --frames 300
# pedestrian close-up i (0..7) in the line-up
CNA_GRAPHICS_RENDERER=VULKAN $R --exec ./build/bin/cna-street --no-audio --no-overlay --lineup \
    --width 1920 --height 1080 --camera "-6.3,1.45,<26+3.4*i>,-1.5708,0.0" --screenshot face.png
```

## Folders

* `easygl/`, `vulkan/` -- the 18 viewpoints and 8 face close-ups from the final build
* `side-by-side/` -- EasyGL left, Vulkan right, per viewpoint and face
* `easygl-before-fix/` -- the EasyGL regression as found (stretched surfaces, no shadows)
* `vulkan-before-fixes/` -- the Vulkan defects as found (SSAO ghost, pass-toggle isolation, shadow
  atlases, shadows before STREET-0007/0008)

## What remains

* Validation: 10 `WARNING-CoreValidation-AllocateDescriptorSets-WrongType` at start-up -- the
  growing descriptor allocator tries the base pool first by design, then chains; no errors.
* `setWorldProperty` on the prepass effect does nothing on either renderer (the prepass reads the
  `uWorld` uniform), so props with a non-identity world matrix are missing from the SSAO prepass
  on both; identical on EasyGL and Vulkan, not changed here.
* Vulkan is CPU-bound in the shadow pass (~48 ms of CPU submission for ~950 caster draws); its GPU
  timer reports 0.00 ms for the shadow stage.
* In CNA, not used by cna-street: other engine-layer SPIR-V programs that skip the Vulkan Y flip
  (clustered forward, particles, punctual casters) and the Vulkan motion-blur camera path.

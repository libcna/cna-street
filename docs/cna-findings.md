# CNA findings

Everything this project ran into in CNA and sharp-runtime that cost time,
produced a wrong picture, or required a workaround. Each entry says what
happened, how to reproduce it, what was done here, and what a framework-level
fix would look like.

Nothing here is a complaint about CNA being incomplete. A framework whose
extension layer already contains cascaded shadow maps, clustered lights, an
analytic sky and a glTF-grade PBR material is a long way past "incomplete"; what
follows is what a real application finds when it uses all of that at once.

Baselines: CNA `next` `7560966`, sharp-runtime `next` `bd282d1`, easy-gl
`develop` `deda7a4`, meta-gl `develop` `2520173`. Renderer `OPENGL33` (EasyGL)
on Mesa 25.2 / llvmpipe.

**This file is the inventory.** What to do about it — every finding re-checked
against CNA at `feat/cnb-source-mipmaps` `3471395`, sorted into blockers and
improvements, with a repair order, the `cna-street` work that each fix unblocks,
and a startup checklist for a session beginning cold — is
`cna-followup-after-framework-work.md`. Read that before starting on any of
these; three of them look different now than they did when they were written,
and CNA-F12 is fixed.

**The `GLTF-2xx` numbers here are this project's, and two of them collide with
CNA's own.** In CNA's tracker `GLTF-207` and `GLTF-208` are about per-part
sampler state, not skin metadata and not a skinned draw. When raising these
upstream, use the aliases `CNASTREET-SKINMETA` (for `GLTF-207` below) and
`CNASTREET-SKINDRAW` (for `GLTF-208` below).

---

## CNA-F1 — vendored single-header include paths resolve into the consumer

**Severity:** high (a consumer cannot build at all without a workaround)
**Affected:** `modules/content/CMakeLists.txt` (and anything else adding
`${CMAKE_SOURCE_DIR}/third_party/...`)

**What happens.** CNA's content module adds its vendored `cgltf` and `stb`
include directories as `${CMAKE_SOURCE_DIR}/third_party/...`. Under
`add_subdirectory()`, `CMAKE_SOURCE_DIR` is the *consumer's* root, so the paths
point at directories that do not exist and the build fails with
`fatal error: cgltf.h: No such file or directory`.

**Reproduction.** Any project that consumes CNA with `add_subdirectory()` and
builds `cna_content`.

**Workaround here.** `cmake/CnaStreetDependencies.cmake` sets the correct paths
with `include_directories()` before `add_subdirectory()`, relying on the fact
that the property is inherited by subdirectories.

**Proposed fix.** Use `${CMAKE_CURRENT_SOURCE_DIR}` (or a `CNA_ROOT`-style
variable captured at CNA's own top level) instead of `${CMAKE_SOURCE_DIR}`.
The same substitution fixes every occurrence.

**Fixed upstream during this work:** no — CNA is consumed read-only here.

---

## CNA-F2 — the module-layout validator tests the consumer's tree

**Severity:** medium
**Affected:** `modules/CMakeLists.txt`

**What happens.** CNA aborts the configure if `${CMAKE_SOURCE_DIR}/src` or
`/include` exists, with a message about a "legacy global `src/` tree
reappear[ing] at the repository root". Under `add_subdirectory()` that is the
consumer's root, so any project using the conventional `src/` + `include/`
layout is refused with a message about CNA's own history.

**Workaround here.** Application sources live under `street/`, and the reason is
stated at the top of `CMakeLists.txt` so nobody moves them back.

**Proposed fix.** Same substitution as CNA-F1.

---

## CNA-F3 — the vendored SDL build cache defaults inside the CNA checkout

**Severity:** low
**Affected:** `cmake/ThirdPartySDL.cmake` (`CNA_SDL_PREBUILT_ROOT`)

**What happens.** The persistent SDL build cache defaults to a directory inside
CNA's own source tree, which writes into a dependency a consumer may reasonably
treat as read-only (a submodule, a shared checkout, a read-only mount).

**Workaround here.** `CNA_SDL_PREBUILT_ROOT` is pointed at the consumer's build
directory, keyed by toolchain.

**Proposed fix.** Default it under `CMAKE_BINARY_DIR`.

---

## CNA-F4 — Linux prerequisites are larger than documented

**Severity:** low (documentation)

**What happens.** A clean Ubuntu 24.04 image needs `libxss-dev`,
`libxkbcommon-dev`, `wayland-protocols`, `libdecor-0-dev`, `libdbus-1-dev`,
`libudev-dev`, `libibus-1.0-dev` and the Mesa/X11 development packages before
the vendored SDL3 will configure; the failure is an SDL `SDL_missing_dependency`
error rather than anything naming CNA. `libzstd-dev` is separately required for
`.cnb` chunk compression and its absence is only a `STATUS` line.

**Proposed fix.** List the full set in the README's prerequisites, and promote
the zstd message to a warning since it silently changes what the content
pipeline can produce.

---

## CNA-F5 — the front-face winding convention is not documented

**Severity:** medium (silent wrong output)

**What happens.** Nothing states which winding CNA's default
`RasterizerState::CullCounterClockwise` keeps. The answer, determined
empirically against `OPENGL33`, is that a face is visible when its vertices are
**clockwise seen from the front** — i.e. `cross(b-a, c-a)` points *away* from
the viewer. That is XNA-faithful and the opposite of the glTF/OpenGL convention,
so geometry authored to the usual convention is invisible, and geometry authored
to it *by accident* is visible but lit from behind.

**Reproduction.** Draw one quad four ways (two windings × two cull modes) and
count the pixels. `docs/architecture.md` records the result.

**Workaround here.** `MeshBuilder::addTriangleIndices` is the single place the
readable counter-clockwise-from-front convention becomes CNA's order, and
`addQuadFacing` takes an intended normal where the corner order is derived from
something else. Both exist because the first version of the road markings and
the roof slopes came out facing into the ground: visible, and lit from below.

**Proposed fix.** State it in the `RasterizerState` documentation, with the
`cross(b-a, c-a)` form rather than "clockwise", which is ambiguous without a
stated handedness.

---

## CNA-F6 — the shadow caster program has no instanced variant

**Severity:** medium (performance)
**Affected:** `CNA::Graphics::ShadowMap`, `CNA::Graphics::CascadedShadowMap`

**What happens.** The caster vertex program takes its world matrix from the
`uWorld` uniform and does not declare the instance-transform attributes the
renderer binds at locations 12–15. So geometry drawn in one call by
`InstancedRendererEXT` cannot be cast in one call: every instance needs its own
`SetUniformMat4("uWorld", …)` and its own draw.

**Consequence here.** Street furniture — lamp columns, bollards, bins, signs,
trees, parked cars — is instanced for the main pass and drawn one at a time in
the shadow pass. That is bounded only by a distance limit (`propShadowDistance`,
default 74 m), which is honest but is a limit the main pass does not need.

**Proposed fix.** Compile the caster with the same
`CNA_GL_INSTANCE_TRANSFORM_DECL` block the stock programs use and multiply
through `cnaInstancePosition`. `uCnaInstanced` already gates it, so the
non-instanced path is unchanged.

---

## CNA-F7 — `AtmosphericSky` renders vertically mirrored

**Severity:** high (visibly wrong output)
**Affected:** `modules/graphics-ext/src/AtmosphericSky.cpp`

**What happens.** The sky's fragment program reconstructs a view ray with

```glsl
vec4 ray = uInverseViewProjection * vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);
```

`FullscreenPass` draws through `SpriteBatch`, whose texture-coordinate origin is
the **top** left of the destination rectangle, while clip space has +1 at the
top. The reconstructed ray is therefore mirrored in Y: the zenith is rendered
underfoot and the ground haze overhead. It is easy to miss because a clear sky
is nearly symmetric about the horizon — until the sun disc appears in the wrong
half of the frame, which is exactly how this was found.

**Reproduction.** Draw `AtmosphericSky` with the sun above the horizon and look
at where the sun ends up.

**Workaround here.** `SkySystem`'s own fragment program flips
`TexCoord.y` before the reconstruction.

**Proposed fix.** `vec2 screen = vec2(TexCoord.x, 1.0 - TexCoord.y);` in
`kFragmentBody`. One line, no API change.

---

## CNA-F8 — `PbrEffect` sRGB-encodes its output by default

**Severity:** high (silently wrong exposure in any HDR pipeline)
**Affected:** `PbrEffect::getEncodeOutputToSrgbEXTProperty` (defaults to `true`)

**What happens.** With the default, the shader's last act is
`FragColor.rgb = cnaLinearToSrgb(FragColor.rgb)`. That is right when the
fragment lands in the back buffer. It is wrong when it lands in
`RenderPipeline`'s float scene target, because `TonemapPass` then reads an
sRGB-encoded value as if it were linear radiance. Asphalt at 0.05 linear becomes
0.25, the tonemapper compresses what is left, and the whole frame flattens: in
this scene a 10:1 albedo ratio between asphalt and paving rendered as 1.6:1.

The failure is very hard to attribute, because the picture is not obviously
broken — it just looks washed out, which is what a hundred other things also
look like.

**Reproduction.** Wrap any `PbrEffect` draw in `RenderPipeline::begin`/`end`
with `setHDREnabled(true)` and compare a dark and a light material.

**Workaround here.** `SceneRenderer::applyMaterial` sets
`setEncodeOutputToSrgbEXTProperty(!pipeline.isUsingSceneTarget())`.

**Proposed fix.** Either default it to `false` (the encode is the *output
stage's* job and `RenderPipeline` already owns one), or have `RenderPipeline`
publish the expected encoding so effects can ask rather than guess. Documenting
it is the minimum: the property's own comment does not mention tonemapping.

---

## CNA-F9 — the shadow atlas is 8-bit and the default depth bias is below one step

**Severity:** medium
**Affected:** `ShadowMap`/`CascadedShadowMap` (atlas format), `PbrEffect`
(`shadowDepthBiasEXT_ = 0.0015f`)

**What happens.** The shadow map stores light-space distance in an ordinary
colour target — a documented and reasonable choice, since not every renderer can
sample a depth attachment. But that makes one quantisation step `1/255 =
0.0039`, and the default depth bias of `0.0015` is *smaller than one step*. Every
surface facing the sun therefore self-shadows.

**Workaround here.** `shadowDepthBias` defaults to `0.0060`.

**Proposed fix.** Default the bias above one quantisation step, or scale it from
the atlas format. A slope-scaled term would be better still: a constant bias
large enough for a grazing surface is large enough to detach the shadow of a
lamp column from its base.

---

## CNA-F10 — `Texture2D::SaveAsPng` cannot save a render target

**Severity:** low (diagnostics)

**What happens.** `SaveAsPng` on a `RenderTarget2D` throws
`no CPU-side pixel data available`. `GetData` works and reads the target back,
so the capability is there; only the convenience path is missing. Debugging a
shadow atlas is exactly the case where you want the convenience path.

**Workaround here.** `SceneRenderer::dumpShadowAtlas` reads back with `GetData`
and re-uploads through `Texture2D::CreateFromPixels` before saving.

**Proposed fix.** Have `SaveAsPng` fall back to `GetData` when there is no CPU
copy.

---

## CNA-F11 — `AtmosphericSky::getModelGlsl()` needs a header the caller must guess

**Severity:** low (documentation)

**What happens.** The static accessor returns the model body only — no
`#version`, no `precision` qualifier. A caller who pastes it into a
`ShaderEffect` gets a compile error about `in`/`out` qualifiers being invalid in
GLSL 1.10, which does not point anywhere near the cause. `AtmosphericSky`'s own
constructor prepends `"#version 300 es\nprecision highp float;\n"`.

**Proposed fix.** Say so in the doc comment, or expose the prologue as a second
accessor.

---

## CNA-F12 — the content pipeline could not produce a mip chain **(fixed in CNA)**

**Severity:** high — it made the content pipeline worse than not using it

**Affected API.** `cna_tool_source_to_cnb`,
`CNA::Content::Cnb::ImportImageAsCnbTexture2D`.

**What happens.** Every texture compiled by `cna_tool_source_to_cnb` has
exactly one mip level. The container is not the limitation: `TEXH` declares a
mip count, `TEXD` carries one payload per level, `CnbMaxTextureMipLevels` is 16
and the `Texture2D` loader uploads every level it finds. Nothing produced one.

**Reproduction.**

```
cna_tool_source_to_cnb road.png road.cnb --name road
cna_tool_cnb_info road.cnb        # -> Texture2D 512x512 Rgba8, 1 level
```

Then load it and look at a road surface at a grazing angle. The whole
carriageway shimmers, the paving speckles, and the façades crawl — worse than
the same textures generated at run time with a chain built by hand, which is
not a trade anyone should have to make. The screenshots that found this are in
the history of this project: the first content build was visibly worse than the
procedural path it replaced.

**Workaround here.** None was adopted. Generating the chain in the application
after loading would need a `GetData` readback per texture and a second upload,
which throws away most of what the pipeline is for.

**Fix.** Contributed to CNA rather than worked around, because the missing
piece belongs in the codec:
`CNA::Content::Cnb::GenerateRgba8MipChain(CnbTextureData&, CnbMipColorSpace)`
box-filters a single-level Rgba8 description into a complete chain, and
`--mipmaps` on the compiler asks for it. The colour space is an argument
rather than an assumption: averaging four sRGB-encoded texels treats an encoded
value as a quantity of light, so a colour map averaged that way dims as it
recedes, while normals, roughness and masks must be averaged exactly as stored.
`--mip-color-space` defaults to `linear`; this project's content build passes
`srgb` for albedo and emissive maps and leaves the rest alone.

**Where.** Commit `347139500` on branch `feat/cnb-source-mipmaps`, pushed to
`openeggbert/cna`, and kept here as
`docs/patches/0001-feat-cnb-generate-mip-chains-when-compiling-a-source.patch`
so this repository carries its own record of what it asked of the framework. It
carries five GTest cases in CNA's own suite; `tests/ContentPipelineTests.cpp`
here exercises the same function, because a regression in it would silently make
this project's content path worse than its procedural one.

---

## CNA-F13 — the light-shaft pass ghosts, and its sample count is not settable

**Severity:** low — visible, and tunable around

**Affected API.** `RenderPipelineSettings::setLightShaftIntensity` /
`setLightShaftThreshold` / `setLightShaftDecay`.

**What happens.** The radial blur that produces the shafts takes a fixed number
of samples, and `decay` decides how far across the screen those samples are
spread. At a decay near the default-ish 0.965 they are far enough apart that
each one lands as a *discrete* smeared copy of whatever seeded it rather than
blending into a ray.

With a threshold low enough to catch a sunlit façade's window frames — 0.82 in
this project's first attempt — the result is a chain of ghost windows climbing
diagonally across the sky from the sun's screen position. It looks exactly like
floating geometry, and it was diagnosed as floating geometry twice before the
FOV-dependence gave it away: a screen-space artefact moves when the field of
view changes and a building does not.

**Reproduction.** Point a camera up at a sunlit façade with the sun just off
screen, `setLightShaftThreshold(0.82f)`, `setLightShaftDecay(0.965f)`.

**Workaround here.** Threshold 0.995 so only the sun disc and the hottest
speculars seed a shaft, decay 0.86 so the samples overlap, intensity 0.32.

**Proposed fix.** Expose the sample count, or scale it with the decay so that a
long shaft is sampled more finely than a short one. Failing that, say in the
doc comment what decay does to the sampling, because the parameter reads like a
purely aesthetic falloff.

---

## CNA-F14 — a skinned figure cannot cast its own shadow

**Where.** `CNA::Graphics::CascadedShadowMap::getCasterEffect()`.

**What happens.** The caster program takes its world matrix from a uniform and
knows nothing about a bone palette, so there is no way to write a skinned mesh
into a cascade in its animated pose. A character rendered with
`SkinnedPbrEffect` therefore has no shadow at all, which on a sunlit pavement is
one of the loudest things a renderer can get wrong: a person with no shadow does
not stand on the ground, they float above it.

**Workaround here.** Every character carries a rigid stand-in in its bind pose,
at half the section count, submitted shadow-only. At the sun angles a street is
lit by, that is a long thin blob on the pavement either way.

**Proposed fix.** A skinned variant of the caster program, taking the same bone
palette uniform `SkinnedPbrEffect` already takes. The vertex work is the same
skinning it already does; only the fragment stage differs.

---

## GLTF-206 — imported glTF textures arrive without a mip chain

**Where.** The runtime glTF path and the compiled `.cnb` path both, for textures
referenced by an imported model.

**What happens.** The application's own surfaces are uploaded with a full mip
chain built by box-filtering the source (see CNA-F12, fixed in CNA for the
`source_to_cnb` path). The images an imported model refers to come in as plain
external `.jpg`/`.png` references and are uploaded at their top level only. A
4K product-shot texture on a 40 cm prop, minified to a few dozen pixels, aliases
into a shimmering mess whenever the camera moves.

**Workaround here.** None. The imported props are small, behind glass, and
culled at 22 m, which keeps the minification ratio low enough that it does not
dominate. It is still visible if you look for it.

**Proposed fix.** Generate mips for model-referenced textures at import, the
same way `cna_tool_source_to_cnb --mipmaps` does for a standalone texture.

**Worked around in the fifth pass, on this side.** `ContentManager` looks for
`<name>.cnb` before it looks for a loose `<name>` (its documented precedence
is xnb > cnb > literal > cnj > loose), and a compiled model refers to its
images by their full file name, `car-honda-civic-ek_tex0.png`. So the content
build now compiles every image a model refers to under that full name --
`car-honda-civic-ek_tex0.png.cnb` -- through `cna_tool_source_to_cnb
--mipmaps`, in the colour space `scripts/model-textures.py` reads off the
model's `.cnj` (sRGB for a base colour or an emissive, linear for a normal,
metallic-roughness or occlusion map), and leaves the loose image beside it.
The model loads exactly as before and its textures arrive with a chain;
`ModelLibrary` logs any map that still has one level. Every scanned prop,
tree and car in the scene now has a mip chain, and the two cars parked
nearest the close viewpoints carry 2k paint that no longer shimmers at
twenty metres. The framework-side fix above is still the right one -- this
is a build step every consumer of the importer would have to repeat.

---

## GLTF-207 — a model's skin is on `SkinsEXT` or on `Tag`, depending on how it was loaded

**Upstream alias: `CNASTREET-SKINMETA`.** CNA's own `GLTF-207` is a different
issue; see the note at the top of this file.

**Where.** `Model::getSkinsEXTProperty()` versus `Model::getTagProperty()`.

**What happens.** The same glTF file, imported by the same importer, exposes its
skeleton through a different API depending on whether it was loaded directly or
compiled to `.cnb` first:

| load path | `SkinsEXT` | `Tag` |
|---|---|---|
| `Load<Model>` on a `.gltf`/`.glb` | populated | first skin, for compatibility |
| `Load<Model>` on a compiled `.cnb` | **empty** | the `SkinningData` |

`SkinsEXT` is the better API — it says which meshes each skin's palette drives,
and `Tag` holds one object and already contends with `ModelAnimationsEXT` for it
(a limitation CNA itself records as GLTF-295). A caller written against
`SkinsEXT`, which is what the header recommends, gets an empty vector from every
model that went through the content pipeline, with no diagnostic. That is the
wrong way round: the compiled path is the one a shipping application uses.

**Reproduction.** Compile any skinned model with `cna_tool_gltf_to_cnb`, load it
with `ContentManager::Load<Model>`, and read `getSkinsEXTProperty()`. The `.cnb`
contains `MSKL` and `MANM` chunks and the skin is there; the collection is empty.

**Workaround here.** `ModelLibrary::loadRig` checks `SkinsEXT` first and falls
back to `dynamic_cast<SkinningData*>(model->getTagProperty())`, taking every
mesh in the model as the skin's when it has to use the fallback — `Tag` does not
say which meshes the palette drives.

**Proposed fix.** Have the `.cnb` model reader populate `SkinsEXT` from the
`MSKL`/`MANM` chunks as the glTF reader does, keeping `Tag` as the compatibility
alias it already is.

---

## GLTF-208 — an imported skinned mesh part draws nothing through `SkinnedPbrEffect`

**Upstream alias: `CNASTREET-SKINDRAW`.** CNA's own `GLTF-208` is a different
issue; see the note at the top of this file. Since this entry was written,
16-bit index element size has been ruled out: the imported models that *do*
draw use it too. `cna-followup-after-framework-work.md` has the full
elimination list and the next experiment.

**Status: unresolved.** Recorded because it is reproducible and because the
alternative was to ship a feature that renders nothing.

**Where.** Drawing an imported `ModelMeshPart`'s own vertex and index buffers
through `SkinnedPbrEffect` with a palette from `AnimationPlayer`.

**What happens.** Nothing is drawn. Everything that can be checked, checks out:

- The model loads. `cesium-man.cnb` reports 1 mesh part, 4 672 triangles, and
  1.531 m tall as authored.
- The skeleton loads: 19 bones with a hierarchy, bind pose and inverse bind
  pose, and one named clip (`Clip0`).
- `AnimationPlayer::StartClip` and `Update` succeed and `GetSkinTransforms()`
  returns 19 well-formed matrices — the first is near-identity with a 3.5 cm
  translation, which is what a root bone should look like.
- The material is opaque, base colour white, with its texture bound.
- The world transform is correct: the figure is submitted at (-7.59, 0.14,
  26.00), which is where it was asked to stand, at a scale of 1.14.
- **The vertex declaration matches the effect's byte for byte**: stride 68,
  Position@0, Normal@12, Tangent@24, TextureCoordinate@40, BlendWeight@48,
  BlendIndices@64, same formats in the same order as
  `VertexPositionNormalTangentTextureSkinned`.

Drawing the same part rigidly through `PbrEffect` also renders nothing, while
every *unskinned* imported model in the same scene renders correctly through
that path — so this is not the skinning, and not the placement.

**What was ruled out.** Winding (double-siding the material changed nothing);
the culling volume (rebuilt from a transformed centre and radius rather than two
transformed corners, which was a real bug in the caller and fixed); the vertex
layout; the palette; the material; the transform.

**What has not been ruled out.** The index buffer's element size — the compiled
part uses 16-bit indices where every generated mesh in this project uses 32-bit
— and the interaction between `getVertexOffsetProperty`/`getStartIndexProperty`
and `DrawIndexedPrimitives` for a part that is not the first in its buffer.

**State here.** `ModelLibrary::loadRig` is kept and is called at start-up, so
the round trip — glTF to `.cnb` to `Model` to `SkinningData` to
`AnimationPlayer` — is exercised and logged on every run. The figure is *not*
placed in the crowd. An invisible pedestrian on the pavement, plus a line in the
documentation claiming it walks there, would be worse than a missing feature.

---

## CNA-F15 — an imported glTF mesh with a single-sided material draws inside out

**Severity:** medium (silent wrong output, and every earlier import hid it)
**Affected:** `GltfImportCore` (winding kept as authored) together with
`RasterizerState::CullCounterClockwise` (CNA-F5's convention)

**What happens.** glTF winds a front face counter-clockwise. CNA's importer
keeps that order (it only reports a mirrored node as "winding unchanged", see
its `mirrored-winding-unapplied` diagnostic) and CNA's default cull state
drops counter-clockwise faces, so an imported part whose material is
`doubleSided: false` renders with its front faces culled: a car body shows
its far flank through its near one, with the interior visible through both.

**Why it went unnoticed for four passes.** Every model imported before the
fourth pass -- the Khronos samples, the twenty Poly Haven scans, the hero
tree -- ships with `doubleSided: true`, under which the cull is off and the
winding does not matter. The first single-sided material to arrive (a
Sketchfab car body, in the fourth pass) drew as an X-ray, and the people
exported from Blender in this project's own format did the same until their
triangles were reversed.

**Reproduction.** Compile any glTF whose materials are single-sided --
Blender's exporter writes `doubleSided: false` whenever a material has
backface culling on -- and draw a part with `CullCounterClockwise`.

**Workaround here.** `scripts/blender-vehicles.py` reverses the index order of
every triangle in the derived GLBs (`flip_winding`), and
`scripts/blender-people.py` emits its triangles in reversed order; the
normals are the author's either way. The renderer is unchanged, so the
generated geometry, which `MeshBuilder` already winds CNA's way, is unaffected.

**Proposed fix.** Either reverse the winding in the importer, so a compiled
glTF part has the same front-face convention as geometry built against CNA's
documented state, or document that an application drawing imported parts
should select `CullClockwise` for them. The first is the one a consumer
would expect: the importer already converts everything else about the file
into CNA's conventions.

---

## GLTF-209 — an instanced draw refuses a mesh with a second UV set or a colour attribute

**Severity:** low (an exception with a clear message, at draw time)
**Affected:** `GraphicsDevice::SetVertexBuffers` (REMED-GFX-202's usage claim)
together with `InstancedRendererEXT`

**What happens.** The device claims each (usage, index) of every bound stream
once, and refuses a stream that repeats *some* of an earlier stream's
usages. The instance-transform stream `InstancedRendererEXT` binds at slot 1
collides with a mesh that carries `TEXCOORD_1..3` or `COLOR_0..1` -- which a
Sketchfab export or a Poly Haven scan's LOD0 (whose colour attribute drives
its wind rig) routinely does -- and the whole instanced group throws
`NotSupportedException` and draws nothing. The same mesh drawn without
instancing is fine.

**Workaround here.** The Blender export steps keep one UV set and no colour
attributes (`blender-vehicles.py`, `blender-tree-lod.py`).

**Proposed fix.** Give the instance stream usages no imported mesh will
carry, or let the importer drop attributes the stock effects do not read.

---

## CNA-F18 — a `ModelMesh` publishes a bounding sphere and no box

**Severity:** medium (silent wrong sizes, and every consumer that needs a box
has to invent one)
**Affected:** `Microsoft::Xna::Framework::Graphics::ModelMesh`
(`getBoundingSphereProperty` only), and through it every application that
culls, collides or scales imported models by their extent

**What happens.** XNA's `ModelMesh` exposes a `BoundingSphere` and CNA
matches it exactly. There is no axis-aligned box, no per-part bounds, and no
way to ask the importer for one. A consumer that needs a box -- and every
consumer that culls against a frustum, sizes a collision solid or scales a
model to a real-world height needs one -- takes the sphere's cube, which is
a box of the model's *diagonal*: on a car, 73 per cent too wide and too
tall. This project reported the Opel Astra as 4.47 x 4.47 x 4.47 m for four
passes.

**What it cost here.** A walking camera stopped two metres short of every
parked car; the driver of a van-class vehicle sat 1.6 m ahead of centre, on
the bonnet; `ModelLibrary::fitTo`, which scales a prop to a stated height
off the box it is given, scaled every prop by the wrong factor; every
imported model was culled and shadow-culled as though it were its own
diagonal.

**Reproduction.** Load any long thin model -- a car -- and compare
`getBoundingSphereProperty().Radius * 2` with the model's own length: they
are equal, and its width and height are neither.

**Workaround here.** `ModelLibrary::PartBounds` reads each part's positions
back from `VertexBuffer::GetDataRawEXT`, which serves the CPU shadow the
buffer keeps of what was uploaded into it, and takes the box of those. A
memcpy per part at load, nothing per frame. The sphere's cube stays as the
fallback for a write-only buffer.

**Proposed fix.** A `BoundingBox` on `ModelMesh` (or on `ModelMeshPart`,
which is the granularity a cull wants), computed by the importer from the
positions it already has, as `BoundingSphere` is. Cheaper there than in
every consumer, and correct there in a way a sphere's cube can never be.

---

## CNA-F19 — the stock-effect draw path re-uploads every parameter and rebinds every texture on every draw

**Severity:** high for a scene of more than a few hundred draws (it is the
whole of this project's opaque-pass submission time)
**Affected:** every stock effect drawn through the EasyGL renderer --
`PbrEffect`, `SkinnedPbrEffect`, `BasicEffect` -- via
`EasyGLRenderer::DrawIndexedPrimitivesEx` / `BindDrawParams`; and the
cascade caster through `ShaderEffect` via `BindCustomEffectMatrices`

**What happens.** `PbrEffect::Apply` itself is cheap: `OnApply` recomputes
the world-view-projection, the fog vector and the diffuse colour under
dirty flags and nothing else. The cost is at the draw. `DrawIndexedPrimitivesEx`
fills a `GpuDrawParams` from the effect and calls `BindDrawParams`, which
selects a program, then sets every uniform the program declares -- the
matrices, the normal matrix it derives, the lights, the material factors,
the fog, the shadow and environment parameters, the texture-transform
matrices -- and rebinds all six texture units, on every draw, with no
comparison against what the previous draw left bound. Two consecutive draws
of the same material differing only in their world matrix pay the same as
two draws of different materials with different textures. Around the draw
itself sit `ConfigureDeclarationForStockProgramEXT` and
`RestoreDeclarationLayoutEXT`, which re-describe the vertex layout to GL per
draw as well.

**Measured here.** The eighth pass timed the opaque pass in two halves from
this side: the material setters plus `Apply` in one, the framework's draw in
the other (`SceneRenderer::Stats::opaqueApplyMs` / `opaqueDrawMs`,
`--frames` prints the split). Flagship view, Radeon 780M, 1600 x 900, the
machine under a load average of six to ten:

| | per frame | per call |
|---|---:|---:|
| this side: ~25 property setters and `Apply`, 1 285 calls | 1.0 ms | 0.8 us |
| the framework's draw, 1 285 calls | 34.8 ms | 27 us |
| of which draws repeating the previous material and environment | 134 | -- |

So a material-state cache on this side would collapse 134 applies and save
about a tenth of a millisecond; the remaining thirty-four are inside the
draw, and only fewer draws moves them. The GPU executes the same pass in
21 - 24 ms. The shadow caster path is the same shape at about 8 us a draw
(`BindCustomEffectMatrices` and a program bind per caster, 1 900 casters a
frame before this pass's culling).

**Reproduction.** Any scene that draws the same material a few hundred
times with different world matrices; time `DrawIndexedPrimitives` against
the same draws issued with a different material each. They cost the same.

**Workaround here.** Fewer draws: material-and-cell batching of the static
street, instancing of every repeated prop, the district batched a strip at
a time (this pass), per-cascade caster culling (this pass). The count is
what this project can move; the per-draw cost is not.

**Proposed fix.** Redundancy elimination in `BindDrawParams`: keep the last
bound `GpuDrawParams` per program and upload only the fields that changed,
and skip texture binds whose unit already holds the texture. The world
matrix and its derived normal matrix are the only things that change
between most consecutive draws in a sorted scene. A per-material uniform
block bound rather than re-uploaded, or a persistent VAO per vertex buffer
so the declaration is not re-described per draw, would each take a further
share. Instanced shadow casters (CNA-F6) and a bone-palette buffer for the
skinned effect (CNA-F14's neighbour) would remove draws that this side
cannot merge.

---

## CNA-F20 — the compiled-model loader gives every mesh part its own `Texture2D` objects

**Severity:** medium (a consumer cannot tell that two parts share an image,
and may be paying for the image more than once)
**Affected:** `ContentManager::Load<Model>` for compiled `.cnb` models, in
`BuildPartEffectEXT` (`ContentManager.cpp`), which resolves each part's
material references and then does
`std::make_unique<Graphics::Texture2D>(cm.Load<Graphics::Texture2D>(asset))`
-- a *copy* of the content manager's cached texture -- for every part, every
map

**What happens.** A model whose parts share an image -- every part of an
atlased car references the same three atlas maps -- comes back with a
distinct `Texture2D` object on every part's `PbrEffect`, with no name set
(`getNameProperty()` is empty), so nothing on the consumer's side can
recognise two parts as drawing from the same image short of reading their
pixels back and hashing them. Whether the copy also duplicates the GPU
storage was not established from outside; the object identity alone is the
problem here.

**What it cost here.** The eighth pass merges a parked authored car's parts
by material at load (`CityScene::mergedByMaterial`), because nothing on a
parked car moves and the wheel nodes the moving copy needs -- nineteen
parts on the Punto, fourteen on the Logan and the Mini -- are nineteen
draws a copy for a car standing still. The merge compares materials by
content (maps, factors, blend, cull) and found no two parts of any of the
eight cars equal, because every part's maps are objects of their own. So
the merge is in place and dormant: it logs once per model that no two parts
share a material, and the parked cars keep their eight to nineteen draws a
copy inside forty-five metres. On the flagship view that is about a hundred
draws that a shared texture would let this side remove.

**Reproduction.** Compile any glTF whose primitives share a material to
`.cnb`, `Load<Model>` it, and compare `getTextureProperty()` across the
parts' effects: distinct pointers, empty names.

**Workaround here.** None taken. Hashing texture contents at load to
recover the identity the loader discarded would work and would be the wrong
kind of code to keep.

**Proposed fix.** Share the cached `Texture2D` between parts (a
`shared_ptr` or a per-model map from asset name to texture) instead of
copy-constructing one per part, and set the texture's `Name` to its asset
name so a consumer can see what it is. Both make the atlas do for the
runtime what it already does for the file.

---

## Eighth visual pass (2026-09-06): environment notes and behaviour

**Two new defects, CNA-F19 and CNA-F20 above. Nothing in CNA,
sharp-runtime, easy-gl or meta-gl was modified.** Three behaviours are
worth knowing.

**A `GpuTimer` on an APU is not independent of the CPU load.** The seventh
pass leaned on the GPU timers as the clock that did not care what the other
sessions were doing. On the Radeon 780M that is only mostly true: the GPU
shares the package's power budget with the CPU, and the same view's GPU
stage sum moved between 38.9 and 50.6 ms across runs whose one-minute load
average moved between 5 and 14, with the counts identical. Not a CNA
matter -- the timer reports what the hardware took -- but a comparison on
this machine wants the load average recorded beside every GPU number,
which the benchmark result now carries, and the counts read first.

**The cascade blend band is in view-depth metres.** `setBlendBand(0.12)`
cross-fades the receiver over the twelve centimetres of depth before each
split, in the shader as `viewDepth > split - uCascadeBlend`, with
`viewDepth` the fragment's view-space Z (`-dot(worldPos, uCascadeViewZ)`).
Both facts matter to an application that culls casters per cascade: the
receiver never reads a cascade for a fragment outside `[previous split -
band, split]`, and "depth" is the perpendicular distance, not the range.
The header says "view-depth units"; the number is metres.

**`RenderPipelineSettings` already carries the two post-process dials
worth having.** `setSSAOSampleCount` (clamped 8..64, default 16) and
`setBloomIterations` (1..8, default 4) exist and were simply not being set
by this project, so SSAO -- a third of the post chain -- ran at the default
on every preset. `applyRenderQualityPresetEXT` derives the bloom depth from
`RenderQuality` but nothing else yet, and `setRenderQuality` alone changes
nothing, which the header says and is easy to miss. The light-shaft pass's
sample count is still not settable (CNA-F13).

---

## Seventh visual pass (2026-09-06): environment notes and behaviour

**One new defect, CNA-F18 above. Nothing in CNA, sharp-runtime, easy-gl or
meta-gl was modified.** Three behaviours are worth knowing.

**A `GpuTimer` range cannot contain another.** `CNA::Graphics::GpuTimer` is
`GL_TIME_ELAPSED`, and GL allows one open query of that target at a time, so
a range round the frame that contained ranges round its stages would be an
error, and a range round the post chain would collide with the chain's own
per-pass timers (`RenderPipeline::setGpuTimingEnabledEXT`). Not a defect:
it is the API's shape. What it means for a consumer is that a frame total is
a sum of stage ranges, that the post chain is measured by the pipeline and
nothing else, and that each timer's result has to be `poll`ed *before* its
range is reopened, because a timer object holds one result and reopening
discards it -- the first version of this project's stage timers polled
after `end` and reported "unavailable" on hardware that has it.

**`PbrEffect::Apply` uploads the whole parameter block.** Whether or not a
property changed since the last `Apply`. The consequence is that a
consumer's own material state cache saves the setters and not the upload,
and the upload is the cost: the two clocks put this project's opaque pass
at 32 ms of submission for 20 ms of execution, 1 436 draws at about 22 us
of driver time each. On this side of the effect the only lever is fewer
draws. A dirty-tracked upload, or a per-material constant buffer bound
rather than re-uploaded, is the framework-side change that would move it.

**The mixer's simultaneous-voice ceiling is not published.** `SoundEffect`
and `SoundEffectInstance` work as XNA's do, `Apply3D` gives attenuation, pan
and an exact Doppler, and a WAV of any bit depth decodes to PCM16 through
SDL. What a consumer cannot find out is how many instances may play at
once before `Play` refuses; `InstancePlayLimitException` exists, and this
project caps its own voices at fourteen and logs a refusal once rather
than finding the number by throwing.

---

## Second visual pass (2026-09-05): environment notes and behaviour

Nothing below is a CNA defect. Each is something the second visual pass ran
into or relied on, recorded because the next session will meet it too. CNA
was consumed read-only throughout; the revision was `next` `34c5a9d`, with
sharp-runtime `next` `c3fbb958`.

**The CNAEXT engine layer is on `next`, not `develop`.** A CNA checkout on
`develop` (`1bb2145` at the time) has nine headers in
`modules/graphics-ext/include/CNA/Graphics/` where `next` has ninety-three;
`CascadedShadowMap`, `RenderPipeline`, `AtmosphericSky`, `EnvironmentProcessor`
and the rest are absent, `develop..next` is 904 commits, and this project does
not compile against it. `dependencies.lock` now says so. CNA resolves
sharp-runtime as its own sibling by default and exposes
`CNA_SHARP_RUNTIME_ROOT` to point it elsewhere, which is how a `next` CNA is
paired with a `next` sharp-runtime when both `develop` checkouts sit beside it.

**`RenderPipeline` sets `BlendState::NonPremultiplied` for the transparent
phase, and a draw inside the phase may override it.** The callback given to
`setTransparentScene` runs with that state bound, and `PbrEffect` does not
premultiply its output by alpha, so a blended surface composites as
`lit * alpha + behind * (1 - alpha)`: a pane of glass at alpha 0.24 kept a
quarter of its reflection. Setting `BlendState::AlphaBlend` (premultiplied:
one, inverse source alpha) from inside the callback before the draw is
honoured and gives `lit + behind * (1 - alpha)`, which is what a reflection
over a transparent layer is. This project does that per material
(`Material::premultipliedBlend`). Worth a line in the `setTransparentScene`
documentation: the blend state is a default, not a contract.

**`ImageBasedLightEXT` may change per draw.** `PbrEffect::setImageBasedLightEXT`
is designed for one environment per scene, but nothing prevents a different
bundle per `Apply`, and the effect rebinds the cubes correctly. The
reflection probes here depend on it: twenty-nine prefiltered cubes at 64 px,
each bound for the draws nearest its position, at no measurable frame cost on
this rasteriser. The three products in a bundle must share one scale, since
`Intensity` is a single multiplier -- a probe cube stored at a different scale
from the sky's irradiance cube would need two.

**`CascadedShadowMap::update` fits from a perspective projection only.** The
fit scales the near corners by `depth / near` to build each cascade's slice,
which is exact for a perspective frustum and meaningless for an orthographic
one. A capture that needs shadows in all directions from a point -- a
reflection probe -- therefore fits the cascades from a camera looking straight
down at the point from 55 m with a wide field of view, so every surface in
the block falls in the same one or two cascades whichever face is drawn. The
receiver selects a cascade by depth along the *fitting* camera, as
`ShadowCascadeStateEXT::CameraView` documents, which is what makes one fit
serve six faces.

**`Texture2D::GetData` on an 8-bit render target is the only readback.** There
is no `GetData` into `Vector4` or `HalfVector4`, so a capture that has to hold
HDR radiance goes through `Color`: this project renders the probe faces at
half brightness with the effect's own sRGB encode on, decodes on the CPU and
re-encodes at the sky cube's scale. A float readback would remove three of
those steps.

---

## Not defects — behaviour worth knowing

* **`CNA_CNAEXT` defaults to `OFF`.** Every `CNA/Graphics/*.hpp` header is
  wrapped in `#ifdef CNA_CNAEXT`, so with the default the entire modern renderer
  compiles to nothing, with no diagnostic. Worth a `STATUS` line at configure
  time.
* **The capability model is good and should be used.**
  `SupportsCapability`, `SupportsRendererFeatureEXT`, `GetRendererLimitEXT` and
  `GetRendererCapabilityReportEXT` between them answer every question this
  project needed to ask before allocating anything.
* **`RenderPipelineSettings::toStringEXT`/`applyFromStringEXT`** is a ready-made
  settings-file format that deserves to be better advertised.

---

## Third visual pass (2026-09-05): environment notes and behaviour

Nothing below is a CNA defect. CNA was consumed read-only throughout at
`next` `e1d3aa5`, with sharp-runtime `next` `c3fbb95`; the pass added scanned
surfaces and scanned props and recalibrated the light, all inside this
repository.

**A `.gltf` with external images imports as cleanly as a `.glb`.**
`cna_tool_gltf_to_cnb` reads a document, its `.bin` and a `textures/` folder
of JPEGs and PNGs, and absorbs the images into the `.cnb` (the compiler
reports "N file(s) absorbed"); `ContentManager::Load<Model>` then hands each
part a `PbrEffect` with its base colour, normal and metallic-roughness maps
bound. Twenty Poly Haven models went through it without a change to the
files. The one thing that did not work was on this side: `MaterialLibrary::add`
overwrote the effect's texture pointers with the null an empty upload returns,
which had left every imported model since the first overhaul untextured. That
is fixed here and recorded in `docs/design-notes.md`.

**GLTF-206 still applies, and scanned props feel it.** A model's textures
arrive at one mip level, so a 1k scan on a 40 cm hydrant aliases past a few
metres. The props here are fetched at Poly Haven's 1k rather than 2k or 4k for
that reason, which is a resolution ceiling this project would rather not have
chosen. Role-aware mip generation at import remains the framework-side fix.

**`ImageBasedLightEXT` accepts an irradiance cube convolved from a different
image than its prefiltered specular.** The probes now hand the effect an
irradiance convolved from the capture multiplied by `probeBounceGain` and a
specular convolved from the plain capture, both at the sky cube's scale under
one `Intensity`, and the effect is indifferent. Worth a line in the
documentation of the bundle: the three products need not come from one image,
only from one scale.

**Blender 4.2+ exports foliage as `alphaMode: BLEND`** whatever the material's
render method says, and CNA honours that faithfully: the leaves draw in the
transparent phase without depth writes and a crown of leaf cards becomes a
ghost. Not a CNA matter -- `scripts/glb-mask-leaves.py` rewrites the material
to MASK before the import -- but the next person importing a Blender tree will
meet it.

**The normal-map convention of this project's own meshes is DirectX.** Also
not a CNA matter, but found while using it: `MeshBuilder`'s tangent handedness
puts the bitangent along increasing v, the catalogue's generator writes green
the same way, and a glTF-convention scan laid on those meshes renders inside
out until its green channel is inverted. Imported models carry their own
tangents and are unaffected. The finding and the test that settled it are in
`docs/design-notes.md`.

## Sixth visual pass (2026-09-06): environment notes and behaviour

**No new CNA defect was found in this pass, and nothing in CNA, sharp-runtime,
easy-gl or meta-gl was modified.** Two behaviours already recorded here came
up again and are worth annotating with what they cost this time; one further
behaviour is new to this file and is not a defect.

**CNA-F6, one shadow draw per instance, is still the shape of the frame.** The
shadow pass was 3 503 draws at the start of this pass and 2 531 at the end,
and every one of them is a `SetUniformMat4` and a `draw`. What this pass did
about it is on this side: `SceneRenderer::drawCasters` now takes the bounding
sphere of the cascade's own slice of the camera frustum rather than a disc
around the camera, so a caster has to be able to reach the ground the cascade
covers before it is submitted. That is a 42 per cent shorter shadow pass and,
over the eighteen screenshot viewpoints, at most one hundredth of one per cent
of the pixels changed. An instanced caster program in CNA would remove the
draw calls rather than the wasted ones, and is still the framework-side fix.

**GLTF-206's workaround now carries a second load.** Every image a model
refers to is compiled under its own full name with a mip chain, which is what
lets this pass put *atlases* into the imported cars and people: an atlas is a
texture whose cells bleed into one another at every mip level unless the
chain is built with padding, and building it on this side is the only way to
know it was built at all.

**Glass is a reflection layer, not a filter.** `PbrEffect` blends a
transparent surface as `lit + behind * (1 - alpha)` -- the reason is recorded
above under the second pass, and it is the right decision for a windscreen
seen from outside. The consequence, met for the first time when this pass put
drivers in the cars, is that *anything behind glass is lit as though the glass
were not there*: a head behind a windscreen gets the full sky, with none of
the loss a real screen and a roof would impose, and reads as a bright egg.
Not a defect -- there is no absorption term in the model to apply -- but the
thing to know is that a material meant to be seen through glass has to carry
that loss in its own colour. The driver tints here are about a stop darker
than the crowd's for exactly that reason.

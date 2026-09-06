# CNA framework follow-up, deferred

**Audience:** a fresh AI-agent session (or developer) picking this up with no
memory of the conversation that produced it. Everything needed is here.

**Question this document answers:** what has to be fixed in CNA before
`cna-street` can make its next major leap, and what should `cna-street` do
immediately afterwards?

---

## 0. STOP — repository concurrency warning

At the time this was written:

* **another agent was actively working on CNA.**
* `cna-street` **deliberately did not modify CNA or sharp-runtime.** Both were
  treated as strictly read-only dependencies for the whole of this phase.
* Every CNA item below was **deferred**, not abandoned, specifically to avoid
  interfering with that concurrent work.
* The framework-fix phase must begin **only after the user confirms that CNA is
  available for modification.**

> **Do not start framework fixes merely because this document exists. First
> confirm that no other agent is currently modifying the relevant CNA working
> tree or branch.**

Separate Git worktrees could technically isolate concurrent work on CNA. This
project intentionally chose *not* to introduce that complexity during this
phase: the cost of a mis-sequenced merge across four repositories is far higher
than the cost of waiting, and the deferral is cheap because everything that was
learned is written down here.

### Repository state when this was written

| Repository | Branch | HEAD | Working tree |
| --- | --- | --- | --- |
| `cna-street` | `claude/cna-city-street-demo-ut9lp1` | `482501a` | clean |
| `cna` | `feat/cnb-source-mipmaps` | `3471395` | dirty submodule `third_party/SDL_image` |
| `sharp-runtime` | `next` | `bd282d1` | clean |

Three things about that table matter:

1. **CNA was not on `next`.** It was on `feat/cnb-source-mipmaps`, the branch
   carrying this project's own CNA-F12 contribution. Every revalidation in
   section 2 was performed against `3471395` on *that* branch, which is neither
   the `next` baseline the original findings were written against
   (`7560966`) nor whatever baseline the next session will find.
2. **CNA's working tree was already dirty.** Do not clean, reset or stash it.
3. **All three SHAs are recorded so a future session can diff against them** and
   see exactly what the other agent changed.

---

## 1. How to read this document

Every issue below carries a **category**, because the distinction changes who
owns the work:

| Category | Meaning |
| --- | --- |
| **Bug** | CNA claims or supports a behaviour and the behaviour is wrong. |
| **Missing capability** | CNA does not currently provide the needed feature. Not a defect. |
| **API inconsistency** | Two legitimate CNA paths expose incompatible representations. |
| **Documentation gap** | The implementation may be correct; the intended usage is unclear. |
| **Application-level need** | Belongs in `cna-street`, not in CNA. |

and a **classification** of its current standing:

| Class | Meaning |
| --- | --- |
| **A** | Still an active CNA defect — needs framework work. |
| **B** | API or documentation gap — may need docs rather than code. |
| **C** | Application limitation — does not belong in CNA. |
| **D** | Already solved upstream — mark resolved, needs revalidation. |
| **E** | Unclear — needs future investigation. |

### ⚠ Issue-ID collision — read this before grepping CNA

`cna-street`'s findings file uses the identifiers `GLTF-206`, `GLTF-207` and
`GLTF-208`. **Only `GLTF-206` means the same thing in CNA.** The other two
collide with unrelated CNA plan items:

| ID | Meaning in `cna-street` | Meaning in CNA (`plans/plan_gltf.md`) |
| --- | --- | --- |
| `GLTF-206` | imported glTF textures have no mip chain | **the same issue** — CNA cites it in `ContentManager.cpp`'s own warning |
| `GLTF-207` | skin metadata lands on `SkinsEXT` or `Tag` depending on load path | **per-part sampler state** (`SamplerStateArrayEXT`) — unrelated |
| `GLTF-208` | an imported skinned mesh part draws nothing | **the file's own sampler reaching the drawn part** — unrelated |

The existing IDs are **not renumbered** here, because they are already cited in
commit messages, code comments and `docs/cna-findings.md`. Instead, use these
aliases whenever talking to CNA or filing anything upstream:

* `GLTF-207` → **`CNASTREET-SKINMETA`**
* `GLTF-208` → **`CNASTREET-SKINDRAW`**

A future agent that greps CNA for "GLTF-208" and finds sampler-state work has
found the wrong thing.

---

## 2. Issue inventory

Seventeen CNA findings exist in `docs/cna-findings.md` (CNA-F1…CNA-F14,
GLTF-206/207/208) plus three "not defects". Each is revalidated below against
CNA `feat/cnb-source-mipmaps@3471395`. **That revalidation is provisional** —
redo it against the new baseline (section 5, step 2).

### Summary table

| ID | Title | Category | Class | Severity | Blocker? |
| --- | --- | --- | --- | --- | --- |
| GLTF-208 / `CNASTREET-SKINDRAW` | imported skinned part draws nothing | Bug (suspected) | E | **Critical** | **yes** |
| GLTF-207 / `CNASTREET-SKINMETA` | skin metadata differs by load path | API inconsistency | A | High | **yes** |
| CNA-F14 | no skinned caster on `CascadedShadowMap` | Missing capability | A (partial D) | High | **yes** |
| GLTF-206 | imported glTF textures have no mip chain | Missing capability | A (known deferral) | Medium | no |
| CNA-F6 | shadow caster has no instanced variant | Missing capability | A | Medium | no |
| CNA-F7 | `AtmosphericSky` renders vertically mirrored | Bug | A | High | no |
| CNA-F8 | `PbrEffect` sRGB-encodes output by default | API inconsistency | A | High | no |
| CNA-F9 | 8-bit shadow atlas, bias below one step | Bug | A | Medium | no |
| CNA-F5 | front-face winding not documented | Documentation gap | B | Medium | no |
| CNA-F11 | `getModelGlsl()` needs an unstated prologue | Documentation gap | B | Low | no |
| CNA-F10 | `SaveAsPng` cannot save a render target | Missing capability | A | Low | no |
| CNA-F13 | light-shaft ghosting, sample count not settable | Missing capability | A | Low | no |
| CNA-F4 | Linux prerequisites larger than documented | Documentation gap | B | Low | no |
| CNA-F1 | vendored include paths resolve into the consumer | Bug | **D?** | High | no |
| CNA-F2 | module-layout validator tests the consumer's tree | Bug | **D?** | Medium | no |
| CNA-F3 | SDL build cache defaults inside the CNA checkout | Bug | **D?** | Low | no |
| CNA-F12 | content pipeline could not produce a mip chain | Bug | **D — fixed** | High | no |

No sharp-runtime defects were ever recorded. `docs/cna-findings.md` mentions
sharp-runtime only in its baseline line. **sharp-runtime needs no work.**

Three findings were added after this table was written and are not
revalidated in section 2; each is written up in full in `cna-findings.md`:

| ID | Title | Category | Severity | Blocker? |
| --- | --- | --- | --- | --- |
| CNA-F15 | an imported single-sided glTF mesh draws inside out under the default cull | Bug | Medium | no |
| CNA-F18 | `ModelMesh` publishes a bounding sphere and no box | Missing capability | Medium | no |
| CNA-F19 | the stock-effect draw path re-uploads every parameter and rebinds every texture per draw | Performance | **High** | no |
| CNA-F20 | the compiled-model loader gives every mesh part its own `Texture2D` objects | Missing capability | Medium | no |

CNA-F19 is the one that matters for the frame. It is where the opaque pass's
submission time goes, it is measured from the application side in the
eighth pass, and nothing on that side can move it further -- see section 3.

---

### GLTF-208 / `CNASTREET-SKINDRAW` — an imported skinned mesh part draws nothing

**Category:** Bug (suspected — see "what is not yet known")
**Class:** E — unclear, needs investigation
**Severity:** **Critical**
**Status:** partially characterized; workaround active (the feature is disabled)

#### `cna-street` impact

This is *the* blocker. It prevents:

* using any authored/rigged glTF human as a pedestrian;
* using any authored rigged asset at all;
* the whole "imported animated character" line of work (post-CNA Phase 3);
* by extension, confidence in the imported-asset path for anything animated.

Everything else in the imported-asset pipeline works: unskinned models load,
draw, are lit by the same sun and shadowed by the same cascade as generated
geometry. Only the skinned path is dead.

#### Existing workaround

`ModelLibrary::loadRig()` is still called at start-up (`CityScene::build`), so
the round trip is exercised and logged on every run:

```
cna-street: imported rig 'cesium-man' -- 1 skinned part(s), 19 bones, clip(s): Clip0
cna-street: imported rig round-trip verified -- 1 skinned part(s), 19 bones,
            clip(s): Clip0; not placed, see cna-findings GLTF-208
```

The figure is **deliberately not placed** in the crowd. Placing an invisible
pedestrian and documenting that it walks the street would be worse than a
missing feature. All pedestrians are the procedural `CharacterFactory` figures.

#### Asset involved

| | |
| --- | --- |
| Local name | `cesium-man` |
| Title | Cesium Man |
| Author | Cesium |
| Licence | **CC-BY-4.0** (attribution required, redistribution allowed) |
| Source | `KhronosGroup/glTF-Sample-Assets`, `Models/CesiumMan/glTF-Binary/CesiumMan.glb` |
| Fetched by | `scripts/fetch-assets.sh` (not committed; 438 044 bytes, SHA-256 in `assets/external/manifest.json`) |
| Compiled to | `assets/content/cesium-man.cnb` (+ `cesium-man_Clip0.cnb`, `cesium-man_tex0.jpg`) |

#### Evidence

Source files involved, all in `cna-street`:

* `street/src/Assets/ModelLibrary.cpp` — `loadRig()`, the skin discovery and the
  `SkinnedGpuMesh` construction.
* `street/include/CnaStreet/Assets/ModelLibrary.hpp` — `ImportedRig`.
* `street/src/Render/SkinnedGpuMesh.cpp` — the borrowing constructor and `draw()`.
* `street/src/Render/SceneRenderer.cpp` — `drawSkinned()`, the per-item state and
  `SetBoneTransforms` / `Apply` / `draw`.

Observed at runtime, all logged and reproducible:

| Checked | Result |
| --- | --- |
| Model loads | 1 mesh part, 4 672 triangles, 1.531 m tall as authored |
| Skeleton loads | 19 bones, hierarchy, bind pose, inverse bind pose |
| Clips | one, named `Clip0` |
| `AnimationPlayer::StartClip` / `Update` | succeed |
| `GetSkinTransforms()` | 19 matrices; `[0]` is near-identity with a 3.5 cm translation — what a root bone should look like |
| Material | opaque, base colour white, texture bound |
| World transform | figure submitted at `(-7.59, 0.14, 26.00)` — exactly where asked — at scale 1.14 |
| Vertex declaration | **identical to the effect's, byte for byte** |
| Drawn output | **nothing** |

The declaration comparison was made by dumping both and printing them as
`usage@offset:format`:

```
imported: 0@0:2  3@12:2  5@24:3  2@40:1  7@48:3  6@64:5   stride 68
wanted:   0@0:2  3@12:2  5@24:3  2@40:1  7@48:3  6@64:5   stride 68
```

The `.cnb` container is well-formed. `cna_tool_cnb_info assets/content/cesium-man.cnb`:

```
asset type   Model (0x00000005)
chunks       11
CMET XREF MDLH MSTR MBON MMSH MMAT MVTX MIDX MSKL MANM
MVTX  222564 bytes      MIDX  28032 bytes
MSKL    3732 bytes      MANM  44044 bytes
external references: 1   cesium-man_tex0.jpg (Texture2D)
```

#### What has already been ruled out — **do not repeat this debugging**

1. **Winding.** Forcing `doubleSided` on the material changed nothing.
2. **The culling volume.** A real bug was found and fixed in the *caller*: a
   `BoundingSphere` built from two *transformed corners* of a box is garbage as
   soon as the transform rotates, because `BoundingBox` does not sort them and
   the sphere from an inverted box has a radius near zero. Rebuilt from a
   transformed centre and radius. The figure still did not appear.
3. **The vertex layout.** Identical, as above.
4. **The bone palette.** 19 well-formed matrices.
5. **The material.** Opaque, white, textured.
6. **The world transform.** Verified by logging the submitted matrix.
7. **16-bit vs 32-bit index buffers — ruled out during this handoff.** The
   earlier finding listed this as an open suspicion. It is not. Derived from the
   `.cnb` chunk sizes against the logged triangle counts:

   | model | triangles | indices | bytes/index | vertex stride | renders? |
   | --- | ---: | ---: | ---: | --- | --- |
   | `cesium-man` | 4 672 | 14 016 | **2.00** | 68 (skinned) | **no** |
   | `avocado` | 682 | 2 046 | **2.00** | 48 | yes |
   | `water-bottle` | 4 510 | 13 530 | **2.00** | 48 | yes |
   | `boombox` | 6 036 | 18 108 | **2.00** | 48 | yes |

   Every compiled model uses 16-bit indices, including the ones that render
   correctly. Index width is not the difference.

8. **Draw-parameter ranges — ruled out during this handoff.**
   `GraphicsDevice::DrawIndexedPrimitives` documents that it throws
   `System::ArgumentOutOfRangeException` "if the requested index range leaves the
   bound index buffer" or "if the declared vertex range leaves the bound vertex
   buffer after its binding offset and `baseVertex`". No exception is raised, and
   the call is not inside a `try`. The declared ranges therefore fit.
   *Note the limit of this:* CNA cannot validate the **decoded index values**,
   only the declared range. Indices pointing outside the vertex range remain
   possible.

9. **Attribute binding by usage — ruled out during this handoff.** EasyGL's
   `RequireDeclarationFitsStockProgramEXT` selects a stock program's input set
   and then "locate[s] each input by XNA usage/index in the caller's
   declaration". The input set is chosen **by stride**: `PbrSkinned` with stride
   68 selects `kPbrSkinned = {Position, Normal, Tangent, UV, BlendWeight,
   BlendIndices}`; `Pbr` with a stride other than 60 selects `kPbr = {Position,
   Normal, Tangent, UV}`. All of those exist in the stride-68 declaration at the
   right offsets. Both the skinned and the rigid draw should therefore bind
   correctly.

10. **The draw code itself.** `GpuMesh::draw` and `SkinnedGpuMesh::draw` are
    byte-identical in what they call:

    ```cpp
    device.SetVertexBuffer(vertexBuffer_);
    device.SetIndexBuffer(indexBuffer_);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, vertexOffset_, 0,
                                 vertexCount_, startIndex_, triangleCount_);
    ```

    and both borrowing constructors read the same six `ModelMeshPart`
    properties. The rigid one draws `water-bottle` correctly. So the *caller* is
    not obviously the difference.

11. **A rigid control draw.** Drawing the cesium-man part through `PbrEffect`
    (no skinning) also renders nothing. **Caveat, stated honestly:** this was
    originally read as proving "not the skinning". Given (9), the control does
    look valid — `kPbr` resolves against the stride-68 declaration — but it has
    *not* been proven that the renderer accepts a declaration carrying elements
    the stock program does not consume. **Re-establish this before relying on
    it.**

#### What has NOT been ruled out

* **The compiled vertex payload.** Nothing has ever read the bytes back. If the
  positions are zero/degenerate, or the blend weights are all zero (so every
  vertex transforms by a zero matrix and collapses to the origin), the symptom
  is exactly "submitted correctly, draws nothing". This is now the **prime
  suspect**, because everything around it has been eliminated.
* **Decoded index values** outside the part's declared vertex range (see 8).
* **`ModelMeshPart` offset semantics** — whether `getVertexOffsetProperty()` is
  in vertices or bytes. For `cesium-man` there is one part so both are 0, which
  makes this *unlikely to be the cause here* but worth confirming.
* **Whether the compiled-CNB skinned path is exercised by CNA at all.** See the
  reproducer below — evidence says it is not.

#### The single most valuable next experiment

**Load the same `.glb` at runtime instead of the compiled `.cnb` and draw it.**
`ContentManager` dispatches on the file extension —
`modules/content/src/Xna/ContentManager.cpp:4150`, `if (ext == ".gltf" || ext == ".glb")`
— so both paths are reachable from the same API with the same asset.

* If the runtime-glTF load **renders**, the defect is in the CNB writer or
  reader, and the search collapses to the `MVTX`/`MIDX`/`MSKL` chunks.
* If it **also fails**, the defect is in the draw path or the effect, and the
  content pipeline is exonerated.

This is one afternoon's work and it halves the problem. Do it first.

The second experiment: read the buffer back.
`VertexBuffer::GetData(VertexPositionNormalTangentTextureSkinned*, int)` exists
(`modules/graphics/include/.../VertexBuffer.hpp:334`). Dump the first few dozen
vertices of the imported part and check that positions are non-degenerate and
blend weights sum to ~1.

#### Minimal future CNA reproducer

CNA's existing coverage does **not** reach this path. The closest test is
`modules/renderers/easygl/examples/easygl_model_skinned_animation_playback_test.cpp`,
which loads a hand-written **`.model.json` + binary fixture** (a 1-bone rig,
vertices 100% bound to bone 0) with `"effect": "SkinnedEffect"`. It exercises
neither `cna_tool_gltf_to_cnb`, nor a `.cnb`, nor `SkinnedPbrEffect`.

The reproducer should therefore be, in bisecting order:

1. Take a minimal skinned `.glb` — one part, one bone, one clip, a handful of
   triangles, all weights 1.0 on bone 0. (`CesiumMan.glb` works as a fallback
   but is larger than necessary and CC-BY-4.0, so a fixture authored inside CNA
   is preferable for a permanent test.)
2. Compile it with `cna_tool_gltf_to_cnb`.
3. `ContentManager::Load<Model>` the `.cnb`.
4. Draw the part with the **stock `SkinnedEffect`**, the same effect the passing
   test uses. *If this fails, the compiled skinned path is broken and
   `SkinnedPbrEffect` is not implicated at all.*
5. Draw the same part with **`SkinnedPbrEffect`**. *If step 4 passes and step 5
   fails, the defect is in `SkinnedPbrEffect` or in its declaration handling.*
6. Load the same `.glb` at **runtime** and repeat 4 and 5, to separate the
   content pipeline from the draw path.

Assert on rendered pixels, not on load success. The failure mode is a *silent*
one: everything reports correct and nothing appears.

#### Suspected root cause

* **Confirmed fact:** the compiled `.cnb` carries `MSKL` and `MANM`, the
  skeleton and clip round-trip correctly, and the vertex declaration matches.
* **Strong suspicion:** the compiled skinned *vertex payload* or the draw of it
  is wrong, because everything else has been eliminated and because no CNA test
  covers glTF → `.cnb` → skinned draw.
* **Hypothesis:** blend weights or positions are not written as the runtime
  expects, so the mesh collapses. Untested — see the experiments above.

#### Proposed framework-level direction

Do **not** prescribe a fix before the bisection above. The architectural layer
depends entirely on which step fails:

* step 4 fails → `CNA::Content::Cnb::CnbModelCodec` (the `MVTX`/`MIDX` writer or
  reader for skinned vertex formats);
* step 5 fails → `SkinnedPbrEffect` / EasyGL's stock-program declaration fit;
* runtime glTF also fails → `GltfImportCore`'s mesh extraction for skinned
  primitives.

#### Required CNA tests

* A rendered-pixel regression test on the glTF → `.cnb` → skinned draw path, for
  both `SkinnedEffect` and `SkinnedPbrEffect`.
* A codec round-trip test asserting that skinned vertex payloads survive
  `.cnb` write/read byte-exactly.

#### `cna-street` cleanup after the fix

* Place the imported rig in the crowd (`CityScene::submitPeople`), restoring the
  code removed in commit `82b38a1` — see section 6, Phase 3.
* Remove the "not placed, see cna-findings GLTF-208" wording from the start-up
  log, `docs/cna-findings.md`, `README.md` (*Known limitations*),
  `assets/external/manifest.json` (`cesium-man`'s `role`) and
  `docs/visual-overhaul/report.md` §6.

#### Acceptance criterion

> A minimal skinned glTF asset, compiled through `cna_tool_gltf_to_cnb` and
> loaded with `ContentManager::Load<Model>`, renders visible animated geometry
> through both `SkinnedEffect` and `SkinnedPbrEffect`, asserted by a
> deterministic rendered-pixel regression test in CNA's own suite.

---

### GLTF-207 / `CNASTREET-SKINMETA` — skin metadata differs by load path

**Category:** API inconsistency
**Class:** A — active
**Severity:** High
**Status:** confirmed; workaround active

#### What happens

The same glTF file, imported by the same importer, exposes its skeleton through
a different API depending on how it was loaded:

| load path | `Model::getSkinsEXTProperty()` | `Model::getTagProperty()` |
| --- | --- | --- |
| `Load<Model>` on `.gltf`/`.glb` | populated | first skin, for compatibility |
| `Load<Model>` on a compiled `.cnb` | **empty** | the `SkinningData` |

Verified read-only against `3471395`:
`modules/content/src/Xna/ContentManager.cpp:4046` calls
`model.setSkinsEXTProperty(...)` **inside the runtime-glTF branch only**, guarded
by `if (hasSkin)`. The `.cnb` resource struct at
`ContentManager.cpp:1881-1887` documents that skinning data is "attached to the
returned Model's own Tag property", with `additionalSkinningData` for extra
runtime glTF skins.

#### Why it is a problem

`SkinsEXT` is the better API and the one the header recommends: it maps each
skin to the meshes its palette drives. `Tag` holds one object and already
contends with `ModelAnimationsEXT` for it — a limitation CNA itself records as
`GLTF-295`. So a caller written against the recommended API silently gets an
empty vector from **every model that went through the content pipeline** — and
the compiled path is the one a shipping application uses. That is the wrong way
round.

#### Existing workaround

`ModelLibrary::loadRig()` checks both, `SkinsEXT` first:

```cpp
const std::vector<ModelSkinEXT>& skins = model->getSkinsEXTProperty();
std::vector<ModelMesh*> skinnedMeshes;
if (!skins.empty() && skins.front().Data != nullptr) { ... }
else if (auto* tagged = dynamic_cast<SkinningData*>(model->getTagProperty())) { ... }
```

Because `Tag` does not say which meshes the palette drives, the fallback takes
**every mesh in the model**. That is right for a character file and would be
wrong for a scene containing a rigged prop among rigid ones.

#### What has been ruled out

Nothing needs ruling out — the behaviour is deterministic and directly
observable. The `.cnb` genuinely contains the skin (`MSKL` + `MANM` chunks are
present and `cna_tool_cnb_info` lists them); only the *presentation* differs.

#### Minimal future CNA reproducer

Compile any skinned model with `cna_tool_gltf_to_cnb`, load the `.cnb`, and
assert `getSkinsEXTProperty()` is non-empty and that
`skins.front().Meshes` names the meshes the palette drives. The same assertion
run against a runtime `.glb` load of the same file must produce an equivalent
result.

#### Suspected root cause

**Confirmed:** the `.cnb` model reader does not populate `SkinsEXT`. It is not a
data-loss problem — the chunks are there — it is an unfinished mapping in the
reader.

#### Proposed framework-level direction

Have the `.cnb` model reader build `ModelSkinEXT` entries from the `MSKL`/`MANM`
chunks exactly as the glTF reader does, keeping `Tag` as the compatibility alias
it already is. The mesh↔skin association has to be recoverable from the
container; if it currently is not, that is a `.cnb` **format** question and the
right answer may be a new chunk or a field in `MSTR` — decide that against the
codec, not from here.

#### Required CNA tests

A single parametrised test that loads the same skinned asset both ways and
asserts the two skin views are equivalent.

#### `cna-street` cleanup after the fix

Delete the `Tag` fallback branch in `ModelLibrary::loadRig()` and the
twelve-line comment above it. Roughly 20 lines removed. Keep the `SkinsEXT`
path exactly as it is.

#### Acceptance criterion

> Application code consuming a skinned model needs no materially different
> skin-discovery logic depending on whether the model came from source glTF or a
> compiled `.cnb`.

---

### CNA-F14 — no skinned shadow caster on `CascadedShadowMap`

**Category:** Missing capability
**Class:** A active on `CascadedShadowMap`; **D already solved on `ShadowMap`**
**Severity:** High
**Status:** confirmed, and **materially better than the existing finding says**

#### The correction this handoff makes

`docs/cna-findings.md` says "a skinned variant of the caster program" is needed.
**That program already exists.** Verified read-only against `3471395`:

* `ShadowMap.hpp:124` — `getSkinnedCasterEffect()`
* `ShadowMap.hpp:150` — `applySkinnedCaster(const std::vector<Matrix>& boneTransforms, int weightsPerVertex)`
* `ShadowMap.cpp:72-83` — the program: `uLightViewProjection * uWorld * skin * vec4(aPosition, 1.0)`
* `plans/plan_modern.md:473` — **`MOD-810` is marked ✅ Done**, with the palette
  passed in rather than read from an effect, `>=2`/`>=4` weight gating and a
  72-bone palette.

**`CascadedShadowMap` does not expose it.** Its public surface has
`getCasterEffect()` and no skinned counterpart, and it is a separate class, not
derived from `ShadowMap`. So the capability exists one class over from where
this project needs it.

This changes the fix from "write a skinned caster" to "**extend MOD-810 to the
cascaded path**", which is a much smaller and much better-specified job.

#### `cna-street` impact

Characters cast a *bind-pose* shadow rather than an animated one. On a sunlit
pavement at the sun angles a street is lit by, the difference is small — a long
thin blob either way — but it costs a whole parallel mesh per character variant.

#### Existing workaround

`CityScene::buildTrafficAndPeople` builds `entry->shadowProxy`: the same figure
at half the section count, with the skinned vertices flattened to rigid ones:

```cpp
entry->shadowProxy = makeProp("person-shadow-" + suffix, [&](GeometryCollector& c) {
    const CharacterFactory::Character proxy = characters.build(look, false);
    for (const CharacterFactory::Character::Part& part : proxy.parts) { ... }
});
```

and `CityScene::submitPeople` submits it shadow-only:

```cpp
if (distance < settings.propShadowDistance)
    submitProp(character.shadowProxy, world, nullptr, true);
```

The application already sets `setWeightsPerVertexProperty(4)`
(`SceneRenderer.cpp:149`) and holds 19-matrix palettes, so it already has
exactly what `applySkinnedCaster` wants — well inside the 72-bone limit.

#### What has been ruled out

Nothing to rule out. The absence is structural and visible in the header.

#### Minimal future CNA reproducer

Extend the MOD-810 verification method already recorded in `plan_modern.md` —
"posing one bone as a pure +4 translation and asserting the shadow lands at the
*posed* position rather than the bind pose" — to a `CascadedShadowMap` pass.
The failure mode is the same one that entry names: a bind-pose shadow looks
entirely correct, just of the wrong thing.

#### Proposed framework-level direction

Mirror `getSkinnedCasterEffect()` and `applySkinnedCaster()` onto
`CascadedShadowMap`, reusing the same program and the same palette contract.
Whether the two classes should share an implementation is a CNA-internal
decision; from a consumer's point of view the only requirement is that the two
shadow classes expose the same caster vocabulary.

#### Required CNA tests

The MOD-810 posed-bone assertion, run through a cascaded pass, ideally on more
than one cascade so the palette upload is proven per-cascade.

#### `cna-street` cleanup after the fix

* Delete `CharacterMesh::shadowProxy` and its construction (~20 lines in
  `CityScene.cpp` around line 1798).
* Replace the shadow-only `submitProp` in `submitPeople` with a skinned shadow
  submission.
* `SceneRenderer::drawShadows` gains a skinned branch calling
  `applySkinnedCaster(*item.bones, 4)`.
* Update `docs/cna-findings.md` CNA-F14, `README.md` *Known limitations*, and
  `docs/design-notes.md` "People: skinned after all".

#### Acceptance criterion

> An animated skinned model casts a shadow whose pose follows the animation,
> through CNA's cascaded shadow path, with no static proxy mesh, covered by a
> regression test.

---

### GLTF-206 — imported glTF textures arrive without a mip chain

> **Fifth-pass note (2026-09-05).** Worked around on the `cna-street` side
> without touching CNA: the content build compiles every image a model
> refers to under its own full file name (`<image>.png.cnb`) with a mip
> chain, and `ContentManager`'s cnb-before-loose precedence makes the model
> pick it up. The framework-side fix below is still the right one -- every
> consumer of the importer would otherwise repeat this build step -- but it
> no longer blocks anything here. See `docs/cna-findings.md`, GLTF-206.

**Category:** Missing capability — **a known, deliberate deferral in CNA**
**Class:** A, but tracked upstream already
**Severity:** Medium
**Status:** confirmed; no workaround

#### The correction this handoff makes

This is **not** an unnoticed defect. CNA knows, warns per map, and states why.
`modules/content/src/Xna/ContentManager.cpp:3657-3675`:

```cpp
// plans/plan_gltf.md GLTF-206: imported PNG/JPEG images have one level. Generating
// the same RGBA box-filter chain for colour, normal and packed-data maps would
// be materially wrong, so the explicit quality deferral is reported per map.
...
"declare a mipmapped minFilter, but CNA imports glTF PNG/JPEG images with one "
"texture level. Role-aware mip generation is deferred, so level zero is used "
"for every LOD and minification quality may be reduced (GLTF-206)."
```

This is the one ID that means the same thing in both repositories.

#### `cna-street` impact

A 4K product-shot texture on a 40 cm prop, minified to a few dozen pixels,
aliases whenever the camera moves. Visible if you look for it; not dominant.

#### Existing workaround

None, and none is wanted. Generating the chain in the application would need a
`GetData` readback and a second upload per texture, which throws away most of
what the content pipeline is for. The imported props are instead small, behind
glass, and culled at 22 m (`CityScene::buildShopDisplays`), which keeps the
minification ratio low.

#### Evidence

The warning above fires during `cmake --build build --target content`. The
affected assets are the sixteen in `assets/external/manifest.json`; the worst
case is `chair-damask` and `plant`, which carry 4K maps.

#### Suspected root cause

**Confirmed and stated by CNA itself:** mip generation for model-referenced
images is deferred because it must be *role-aware*. Averaging four sRGB-encoded
texels treats an encoded value as a quantity of light, so a colour map averaged
that way dims as it recedes, while normals, roughness and masks must be averaged
exactly as stored.

#### Proposed framework-level direction

The machinery already exists and this project contributed it. CNA-F12 added
`CNA::Content::Cnb::GenerateRgba8MipChain(CnbTextureData&, CnbMipColorSpace)`
with the colour space as an *argument* rather than an assumption, and
`cna_tool_source_to_cnb --mip-color-space {linear,srgb}`. The glTF import path
needs to call the same function, choosing the colour space from the **material
slot** the image is bound to: base colour and emissive are sRGB; normal,
metallic-roughness, occlusion and every packed map are linear.

That mapping is exactly what `cna-street`'s content build already does by hand
for its own surfaces (`cmake/CnaStreetBuildContent.cmake`), so the rule is known
to work.

#### Required CNA tests

Compile a glTF whose material binds a base-colour map and a normal map, and
assert that the resulting textures have a full chain, that the base-colour chain
was averaged in sRGB and the normal chain in linear.

#### `cna-street` cleanup after the fix

Nothing to remove — there is no workaround. The 22 m display cull could be
relaxed if desired, but it was chosen for draw-call reasons as much as for
aliasing, so leave it.

#### Acceptance criterion

> A glTF asset compiled through the normal CNA content pipeline produces
> textures with the expected mip chain, with the colour space chosen per
> material slot, equivalent to standalone texture processing.

Note: this formulation says "with the colour space chosen per material slot"
rather than plainly "equivalent to standalone texture processing", because CNA's
own comment establishes that a single uniform chain would be *materially wrong*.

---

### CNA-F7 — `AtmosphericSky` renders vertically mirrored

**Category:** Bug · **Class:** A active · **Severity:** High

**Revalidated:** still present. `modules/graphics-ext/src/AtmosphericSky.cpp:130`:

```glsl
vec4 ray = uInverseViewProjection * vec4(TexCoord * 2.0 - 1.0, 1.0, 1.0);
```

with no Y flip. `FullscreenPass` draws through `SpriteBatch`, whose texture
coordinate origin is the **top** left, while clip space has +1 at the top — so
the reconstructed ray is mirrored in Y and the zenith renders underfoot.

**Impact / workaround:** `cna-street` does not use `AtmosphericSky`'s own draw;
`SkySystem` has its own fragment program that flips `TexCoord.y` before the
reconstruction. The CPU-side evaluation is used unmodified.

**Fix:** `vec2 screen = vec2(TexCoord.x, 1.0 - TexCoord.y);` in `kFragmentBody`.
One line, no API change.

**Test:** render with the sun above the horizon and assert the bright disc is in
the upper half of the frame.

**Cleanup after fix:** `SkySystem` could drop its private flip, but it has its
own cloud layer in the same program, so the saving is one line and the risk is a
double flip. **Low priority; verify visually before changing.**

**Acceptance:** the stock `AtmosphericSky` draw puts the sun where the sun is.

---

### CNA-F8 — `PbrEffect` sRGB-encodes its output by default

**Category:** API inconsistency · **Class:** A active · **Severity:** High

**Revalidated:** still present. `PbrEffect.hpp:580` — `bool encodeOutputToSrgb_ = true;`
with `getEncodeOutputToSrgbEXTProperty` / `setEncodeOutputToSrgbEXTProperty` at
lines 400/406.

**Why it matters:** correct for a back-buffer draw, wrong for
`RenderPipeline`'s float scene target, where `TonemapPass` then reads an
sRGB-encoded value as linear radiance. In this scene a 10:1 albedo ratio
rendered as 1.6:1 — the frame does not look broken, it looks *washed out*, which
is what a hundred other things also look like.

**Workaround:** `SceneRenderer::applyMaterial` sets
`setEncodeOutputToSrgbEXTProperty(!pipeline.isUsingSceneTarget())`.

**Fix direction:** either default to `false` — the encode is the output stage's
job and `RenderPipeline` already owns one — or have `RenderPipeline` publish the
expected encoding so effects can ask rather than guess. Documenting it is the
minimum; the property's own comment does not mention tonemapping.

**Cleanup after fix:** one line in `applyMaterial`, **only if** CNA picks the
"pipeline publishes the encoding" option. If the default merely flips, keep the
explicit set — being explicit about a colour-space decision is not a workaround.

**Acceptance:** a `PbrEffect` draw into an HDR scene target produces linear
radiance without the application setting anything.

---

### CNA-F9 — 8-bit shadow atlas with a default bias below one quantisation step

**Category:** Bug · **Class:** A active · **Severity:** Medium

**Revalidated:** still present. `PbrEffect.hpp:544` — `shadowDepthBiasEXT_ = 0.0015f`.
The atlas stores light-space distance in an ordinary colour target, so one
quantisation step is `1/255 = 0.0039` — the default bias is **smaller than one
step**, and every surface facing the sun self-shadows.

**Workaround:** `shadowDepthBias` defaults to `0.0060` in `RenderSettings`.

**Fix direction:** default the bias above one quantisation step, or scale it from
the atlas format. A slope-scaled term would be better still: a constant bias
large enough for a grazing surface is large enough to detach a lamp column's
shadow from its base.

**Cleanup after fix:** revisit the 0.0060 default; likely keep an explicit value
but stop needing it to be this large.

**Acceptance:** a flat lit surface does not self-shadow at the default bias.

---

### CNA-F6 — the shadow caster program has no instanced variant

**Category:** Missing capability · **Class:** A active · **Severity:** Medium

**Revalidated:** `ShadowMap.cpp` declares `uniform mat4 uWorld` and no instance
transform attributes (lines 45-48 rigid, 72-83 skinned).

**Impact:** measured. `docs/visual-overhaul/performance.md` records **2 819
shadow draws for 1 356 visible objects**, because an instance group drawn in one
call by `InstancedRendererEXT` must be cast one copy at a time into every cascade
that reaches it. This is the second-largest remaining cost in the frame.

**Workaround:** a distance limit (`propShadowDistance`, default 74 m) the main
pass does not need.

**Fix direction:** compile the caster with the same
`CNA_GL_INSTANCE_TRANSFORM_DECL` block the stock programs use and multiply
through `cnaInstancePosition`. `uCnaInstanced` already gates it, so the
non-instanced path is unchanged.

**Cleanup after fix:** `SceneRenderer::drawShadows`'s per-instance loop becomes
one instanced call per group; `propShadowDistance` can be relaxed.

**Acceptance:** an instance group casts in one draw per cascade, and the shadow
draw count falls to roughly the visible-batch count.

---

### CNA-F5, CNA-F11, CNA-F4 — documentation gaps

**Class:** B for all three. None blocks anything; all cost real time once.

* **CNA-F5 — front-face winding is not documented.** Determined empirically:
  a face is visible when its vertices are **clockwise seen from the front**, i.e.
  `cross(b-a, c-a)` points *away* from the viewer. XNA-faithful, and the opposite
  of the glTF/OpenGL convention — so geometry authored to the usual convention is
  invisible, and geometry authored to it by accident is visible but lit from
  behind. Both happened here. **Fix:** state it in the `RasterizerState`
  documentation using the `cross(b-a, c-a)` form; "clockwise" alone is ambiguous
  without a stated handedness.
* **CNA-F11 — `AtmosphericSky::getModelGlsl()` needs a prologue the caller must
  guess.** Returns the model body with no `#version` and no precision qualifier;
  pasting it into a `ShaderEffect` fails with an error about `in`/`out` in GLSL
  1.10 that points nowhere near the cause. **Fix:** say so in the doc comment, or
  expose the prologue as a second accessor.
* **CNA-F4 — Linux prerequisites are larger than documented.** A clean Ubuntu
  24.04 image needs `libxss-dev`, `libxkbcommon-dev`, `wayland-protocols`,
  `libdecor-0-dev`, `libdbus-1-dev`, `libudev-dev`, `libibus-1.0-dev` and the
  Mesa/X11 development packages before the vendored SDL3 will configure, and the
  failure names SDL rather than CNA. `libzstd-dev` is separately required for
  `.cnb` chunk compression and its absence is only a `STATUS` line — it silently
  changes what the content pipeline can produce, so it deserves a warning.

---

### CNA-F10 — `Texture2D::SaveAsPng` cannot save a render target

**Category:** Missing capability · **Class:** A active · **Severity:** Low

**Revalidated:** `modules/graphics/src/Xna/Texture2D.cpp:1438` still throws
`Texture2D::GetData: no CPU-side pixel data available`. `GetData` on a render
target works, so the capability is there; only the convenience path is missing —
and debugging a shadow atlas is exactly when you want the convenience path.

**Workaround:** `SceneRenderer::dumpShadowAtlas` reads back with `GetData` and
re-uploads through `Texture2D::CreateFromPixels` before saving.

**Fix:** have `SaveAsPng` fall back to `GetData`. **Cleanup:** ~10 lines.

---

### CNA-F13 — light-shaft ghosting, sample count not settable

**Category:** Missing capability · **Class:** A active · **Severity:** Low

The radial blur takes a fixed number of samples and `decay` decides how far
across the screen they spread; at a high decay each lands as a discrete smeared
copy of whatever seeded it. With a threshold low enough to catch a sunlit
façade's window frames, the result is a chain of ghost windows climbing across
the sky — diagnosed as floating geometry twice before the FOV-dependence gave it
away.

**Workaround:** threshold 0.995, decay 0.86, intensity 0.32 — only the sun disc
and the hottest speculars seed a shaft, and the shaft is short enough for the
samples to overlap.

**Fix:** expose the sample count, or scale it with the decay. Failing that,
document what decay does to the *sampling*; the parameter reads like a purely
aesthetic falloff.

---

### CNA-F1, CNA-F2, CNA-F3 — build integration · **Class D? — probably fixed, must revalidate**

These three were high-friction consumer-integration defects. Reading CNA at
`3471395` suggests **all three have been addressed upstream**, but `cna-street`
still carries its workarounds, which would mask a fix:

| ID | Evidence it may be fixed |
| --- | --- |
| CNA-F1 | `modules/content/CMakeLists.txt:19` now reads *"CNA_SOURCE_DIR, not CMAKE_SOURCE_DIR: … CMAKE_SOURCE_DIR points at the consumer's root whenever CNA is used via add_subdirectory"* |
| CNA-F2 | `modules/CMakeLists.txt:397` carries the same reasoning, naming a consumer that does exactly that |
| CNA-F3 | `cmake/ThirdPartySDL.cmake` now has a `_cna_sdl_prebuilt_legacy_default` check, implying the default moved and the old one is detected |

**How to revalidate — do this, do not assume:** temporarily remove the
workarounds and configure from clean.

* CNA-F1 → delete the `include_directories()` block in
  `cmake/CnaStreetDependencies.cmake`; a clean configure+build must succeed.
* CNA-F2 → create an empty `src/` at the `cna-street` root and configure; it must
  not abort. (Do not move the sources; `street/` is fine where it is.)
* CNA-F3 → unset `CNA_SDL_PREBUILT_ROOT` and confirm nothing is written inside
  the CNA checkout.

If they pass, mark all three **resolved** in `docs/cna-findings.md`, delete the
workarounds, and note the CNA SHA that fixed them.

---

### CNA-F12 — content pipeline could not produce a mip chain · **Class D — FIXED**

Fixed by this project and contributed to CNA:
`CNA::Content::Cnb::GenerateRgba8MipChain(CnbTextureData&, CnbMipColorSpace)` and
`cna_tool_source_to_cnb --mipmaps --mip-color-space {linear,srgb}`.

Commit `347139500` on branch `feat/cnb-source-mipmaps`, pushed to
`openeggbert/cna`, and kept here as
`docs/patches/0001-feat-cnb-generate-mip-chains-when-compiling-a-source.patch`.
Five GTest cases in CNA's own suite; `tests/ContentPipelineTests.cpp` exercises
the same function from this side.

**Action for the future session:** confirm this landed on whatever the new
baseline is. If the other agent's work rebased or dropped it, the whole content
pipeline silently gets worse than the procedural path it replaced — that is what
CNA-F12 originally was.

---

### Not defects — behaviour worth knowing

* **`CNA_CNAEXT` defaults to `OFF`.** Every `CNA/Graphics/*.hpp` is wrapped in
  `#ifdef CNA_CNAEXT`, so with the default the entire modern renderer compiles to
  nothing, with no diagnostic. Worth a `STATUS` line at configure time. (Class B.)
* **The capability model is good and should be used.** `SupportsCapability`,
  `SupportsRendererFeatureEXT`, `GetRendererLimitEXT` and
  `GetRendererCapabilityReportEXT` answer every question this project needed to
  ask before allocating anything.
* **`RenderPipelineSettings::toStringEXT`/`applyFromStringEXT`** is a ready-made
  settings-file format that deserves better advertising.

---

## 3. Must fix / strong improvement / optional

### Must fix before the next flagship `cna-street` phase

These are genuine blockers — each one prevents a specific planned piece of work,
and they are ordered by dependency, not by severity.

1. **GLTF-208 / `CNASTREET-SKINDRAW`.** Without it there is no imported animated
   character, and Phase 3 of the post-CNA roadmap cannot start at all.
2. **GLTF-207 / `CNASTREET-SKINMETA`.** Not a hard blocker — the fallback works —
   but it must be fixed *with* GLTF-208, because both live in the same code and
   fixing one without the other means touching `loadRig` twice.
3. **CNA-F14 (cascaded skinned caster).** An imported animated character with a
   bind-pose shadow is a worse result than the current procedural one. This is a
   blocker *for the quality of Phase 3*, not for its start.

**GLTF-206 is deliberately not on this list.** It degrades imported props; it
does not prevent anything. Fix it when convenient.

### Strong CNA improvements — not blockers, high value

* **CNA-F19 — per-draw parameter upload in the stock-effect draw path.** The
  largest measurable performance item left, and the one this side has run
  out of levers against: the opaque pass on the Radeon 780M is ~1 300 draws
  at ~27 us of driver time each (35 ms of submission for 21 - 24 ms of GPU
  execution), and the eighth pass measured this side's own material setters
  and `Apply` at 0.8 us of that. `BindDrawParams` re-uploads every uniform
  and rebinds every texture unit on every draw with no redundancy check;
  fewer draws is the only application-side remedy and the street has been
  batched, instanced and culled to about 1 300 - 1 600 of them. Added in the
  eighth pass; see `cna-findings.md` CNA-F19 for the measurement.
* **CNA-F6 — instanced shadow casting.** Still the shape of the shadow pass:
  the eighth pass's per-cascade caster culling took the flagship view from
  1 900 shadow draws to about 870 without a visible change, and every one of
  those is still a `SetUniformMat4` and a draw. It was 2 819 shadow draws
  for 1 356 visible objects when this document was first written.
* **CNA-F8 — the sRGB-encode default.** The single most expensive-to-diagnose
  class of bug in the whole project.
* **CNA-F7 — the mirrored sky.** One line, visibly wrong output.
* **CNA-F9 — shadow bias and atlas precision.**
* **GLTF-206 — role-aware mip generation on import.**

### Optional / future CNA enhancements

Do **not** assume CNA lacks these — several exist and were simply not the right
tool for this scene. Check the current tree before proposing any of them.

* **Reflection probes / planar reflections.** `cna-street` has no reflection
  beyond the sky IBL, and that is the single most-missed cue at eye level. What
  CNA currently offers here was not fully surveyed.
* **SSR.** `SsrPass` exists and `setSSREnabled` is wired. It was **tried and
  dropped** — see section 4.
* **Contact shadows.** `ContactShadowPass` exists in `graphics-ext` and was never
  evaluated.
* **Volumetric fog** (`setVolumetricFogDensity` exists), **colour grading**
  (`setColorGradeEnabled` exists), **film grain**, **chromatic aberration**,
  **lens flare**, **motion blur**, **depth of field** — all present in
  `RenderPipelineSettings` and all unused by this project.
* **Clustered/many-light rendering.** `ClusteredForwardEffect`,
  `ClusteredLightSetEXT` and friends exist. `cna-street`'s night mode bakes its
  light instead, because `PbrEffect` carries **one** punctual light per draw and
  the street has forty lamps. Whether the clustered path is a practical
  replacement was never established.
* **TAA, GPU-driven rendering.** Not surveyed.

---

## 4. Negative results — things that were tried and did not work

Preserved because re-trying them is pure waste.

**Screen-space reflections do not help this scene.** `setSSREnabled(true)` was
tested on the shop-window, car and road viewpoints:

* on the shop window it **washes the interior out** and cannot reach the glass at
  all — the glazing is alpha-blended and drawn *after* the pass;
* on the road and the cars it changes **nothing visible**, because the asphalt is
  far rougher than any SSR roughness cutoff and a black car reflecting dark
  asphalt has nothing to show.

The pass works. This scene has no surface for it. If reflections are wanted,
**probes or planar reflections are the right tool**, not SSR.

**A per-material shadow-distance cap buys almost nothing here.** Implemented,
measured, and kept with its uselessness written into the code: one plot's window
frames are a *single* batch spanning a whole elevation, and a distance test
against a batch takes its nearest corner — so an elevation running from 20 m to
60 m is kept whole. Per-batch culling can only ever be as fine as the batches,
and these are coarse on purpose. **Do not "improve" this without changing the
batching first.**

**Double-siding the imported skinned material did not make it appear.** See
GLTF-208, ruled out (1).

**The frame time was measured with the wrong instrument once.** The debug
overlay's headline is an *exponential* average; it read 214 ms on a frame that
took 778, and an earlier 122 ms baseline was the same error. Every performance
number now comes from `--frames N`. `docs/visual-overhaul/audit.md` carries the
correction. **Do not quote the overlay in a report.**

---

## 5. Proposed milestone — *CNA Production Asset / Graphics Hardening for cna-street*

Strict ordering. Each step depends on the ones before it.

1. **Establish the current CNA baseline.** Only after the user confirms the other
   agent has finished. Record branch and HEAD SHA for `cna`, `sharp-runtime`,
   `easy-gl`, `meta-gl` and `cna-street`.
2. **Revalidate every issue in section 2 against that baseline.** Do not skip
   this. CNA-F1/F2/F3 already look fixed; others may be too, and some may have
   changed shape. Mark each finding resolved / still-open / changed, with the SHA.
3. **Create minimal failing CNA regression cases** for whatever is still open.
   Failing tests first — a fix without a test that failed before it is a fix
   nobody can defend later.
4. **Fix GLTF-208 / `CNASTREET-SKINDRAW`.** Bisect with the six-step reproducer
   in section 2 *before* writing any fix. Start with the runtime-glTF-versus-CNB
   A/B; it halves the search space in one experiment.
5. **Fix GLTF-207 / `CNASTREET-SKINMETA`.** Same code region as 4; do them
   together.
6. **Fix CNA-F14** — extend MOD-810's skinned caster to `CascadedShadowMap`.
7. **Fix GLTF-206** — role-aware mip generation on the glTF import path, reusing
   `GenerateRgba8MipChain`.
8. *(Optional, same visit)* **CNA-F6, CNA-F7, CNA-F8, CNA-F9.** Cheap, isolated,
   and each removes a `cna-street` workaround.
9. **Run CNA's full relevant test suite.**
10. **Build `cna-street` against the fixed CNA, unchanged**, and confirm nothing
    regressed before removing anything.
11. **Remove obsolete `cna-street` workarounds** — see section 6, Phase 2.
12. **Only then** consider larger new graphics capabilities.

**Dependency note.** Imported pedestrians must not be the first `cna-street`
task: if the imported skinned path still does not render, every hour spent on
asset selection, LOD strategy and placement is spent on something that cannot be
seen. Steps 4–6 gate Phase 3 of section 6 entirely.

---

## 6. `cna-street` work immediately after the CNA fixes

### Phase 1 — integrate the fixed baseline

* Update the CNA dependency to the fixed commit; record the SHA in
  `docs/cna-findings.md` and `plan.md`.
* Clean rebuild, Debug **and** Release. Both must be warning-free in this
  project's own code — they are today.
* `rm -rf assets/content && cmake --build build --target content` — a full content
  rebuild from source. Expect ~202 surfaces and 11 models.
* `python3 scripts/validate-assets.py` — must report 0 problems.
* `ctest --test-dir build --output-on-failure` — all nine suites.
* Capture a baseline screenshot set **before changing anything**, so every later
  change has a before.

### Phase 2 — remove workarounds, one at a time, each proven unnecessary

Remove **only** what the corresponding fix made obsolete, and re-render the
affected viewpoint after each removal:

| Workaround | Where | Removable after |
| --- | --- | --- |
| `Tag` fallback in skin discovery | `ModelLibrary::loadRig` | GLTF-207 |
| "not placed" guard + all its documentation | `CityScene`, README, manifest, report | GLTF-208 |
| `CharacterMesh::shadowProxy` and its submission | `CityScene` (~1798, ~631) | CNA-F14 |
| `include_directories()` shim | `cmake/CnaStreetDependencies.cmake` | CNA-F1 |
| `street/` layout constraint (the *comment*, not the layout) | root `CMakeLists.txt` | CNA-F2 |
| `CNA_SDL_PREBUILT_ROOT` override | build config | CNA-F3 |
| `SkySystem`'s private `TexCoord.y` flip | `SkySystem.cpp` | CNA-F7 — **verify visually; the cloud layer shares the program** |
| `setEncodeOutputToSrgbEXTProperty` call | `SceneRenderer::applyMaterial` | CNA-F8 — **only** if CNA publishes the expected encoding |
| `shadowDepthBias = 0.0060` | `RenderSettings` | CNA-F9 |
| `dumpShadowAtlas` readback+reupload | `SceneRenderer` | CNA-F10 |
| per-instance shadow loop | `SceneRenderer::drawShadows` | CNA-F6 |

**Do not remove anything that is a deliberate choice rather than a workaround.**
Being explicit about a colour-space decision is not a workaround. Neither is
`MeshBuilder::addQuadFacing` — CNA-F5 is a documentation gap, and stating an
intended facing is good code regardless.

### Phase 3 — imported animated pedestrian

Only after GLTF-208 and CNA-F14 are validated.

* An authored, legally redistributable rigged human through the normal pipeline:
  fetch → manifest entry with a verified per-asset licence →
  `cna_tool_gltf_to_cnb` → `Load<Model>` → `SkinnedPbrEffect` → animated shadow.
* Requires an LOD strategy from the start. A skinned figure **cannot be
  instanced** — each carries its own bone palette — so it costs one draw per
  material at any distance. The existing three-tier approach (full set under
  20 m, collapsed material set beyond, own cull distance at 130 m) is the
  starting point, and `docs/visual-overhaul/performance.md` has the measurements.
* CesiumMan stays as the **regression asset**, not as the shipped pedestrian: one
  part, 19 bones, 4 672 triangles, CC-BY-4.0.

**Keep the procedural `CharacterFactory` figures.** They are not scaffolding.
They are the fallback when assets are not fetched, the low-quality mode, a
demonstration of procedural generation through CNA's skeletal path, and the
regression baseline. `docs/design-notes.md` records three modelling bugs they
cost to find; deleting them throws that away.

### Phase 4 — imported authored vehicles

High-quality, legally redistributable glTF vehicles for Ultra/flagship. Several
distinct silhouettes — a hatchback, a saloon, a van at minimum.

**Keep `VehicleFactory`.** It becomes the lower LOD, the low/medium quality mode,
the fallback, and the procedural showcase. It is twelve variants over six classes
built from monotone-cubic profile curves; it is not disposable.

### Phase 5 — vegetation assets

Authored trees where they beat the procedural ones — which is not obvious, and
should be judged from a rendering rather than assumed. Retain instancing and the
existing distant-mesh LOD. The procedural trees carry a real technique (vertex
normals bent toward the crown centre so a canopy shades like a ball) that an
imported billboard tree will not have.

### Phase 6 — reflections

Evaluate what CNA provides *after* the framework work, then choose. Suggested
priority:

1. **Local reflection probes** — general benefit to every vehicle and every pane.
2. **Planar reflection** for one or two important storefront windows.
3. **SSR** only where it demonstrably helps — and section 4 says it did not.

**Do not build all three.** Build one, measure it, and look at it.

### Phase 7 — true many-light night mode

Only if the clustered/many-light path proves practical after the framework work.
Replace the baked emissive lighting with real PBR interaction for street lamps,
shop interiors, headlights, brake lights and traffic signals.

Keep the baked path as the low/medium mode. Measure before and after — night
currently costs **3%** of the frame, and a many-light path that costs 40% for a
better-lit pavement is a decision, not an upgrade.

### Phase 8 — final realism pass, application-only

Building microdetail, façade variation, glass, interiors, decals, dirt, road and
sidewalk wear, street clutter, lighting calibration, distant context, final
flagship screenshots. Nothing here needs CNA.

---

## 7. Anti-goals — what must NOT happen

* **Do not rewrite `cna-street`.** The architecture works; the visual overhaul
  was achieved without replacing it.
* **Do not remove procedural systems that remain useful.** `CharacterFactory`,
  `VehicleFactory`, `PropFactory` and `TextureFactory` are fallbacks, LOD tiers,
  low-quality modes and regression baselines.
* **Do not turn CNA fixes into street-specific hacks.** If the fix belongs in the
  framework, it goes in the framework or it does not go in.
* **Do not implement a private renderer beside CNA.**
* **Do not bypass CNA for imported models.** Writing a glTF parser here would
  defeat the entire point of the exercise.
* **Do not add framework features for API-count vanity.** Every EXT feature this
  project uses does real work in the frame.
* **Do not reopen solved issues without evidence.** CNA-F12 is fixed. CNA-F1/F2/F3
  are probably fixed.
* **Do not assume any finding here still applies.** Another agent has changed
  CNA. **Revalidate first** — that is step 2 of the milestone and it is not
  optional.
* **Do not quote the debug overlay's average as a performance number.** Use
  `--frames N`.
* **Do not commit the fetched external assets.** 110 MB of somebody else's work
  in permanent history is a separate permission from using it.

---

## 8. Fresh-context startup checklist

For an agent opening this repository with no prior context:

```text
 1. Read docs/cna-followup-after-framework-work.md (this file) end to end.
 2. Read docs/cna-findings.md for the full issue detail.
 3. CONFIRM WITH THE USER that CNA is no longer being modified by another agent.
    Do not proceed without this.
 4. git status in cna-street, cna, sharp-runtime. Do NOT reset, clean or stash
    anything you did not create — CNA's tree was already dirty when this was
    written.
 5. Record branch + HEAD SHA for all three (plus easy-gl and meta-gl).
    Compare against section 0's table to see what changed.
 6. Build cna-street UNCHANGED, Debug and Release. It must build warning-free.
 7. Run ctest --test-dir build --output-on-failure. Nine suites, all passing.
 8. Run python3 scripts/validate-assets.py. Zero problems expected.
 9. Capture a screenshot baseline before touching anything:
       xvfb-run -a ./build/bin/cna-street --capture <dir> --width 1024 --height 576 --no-overlay
10. Revalidate EVERY issue in section 2 against the new CNA baseline.
    Mark each resolved / still-open / changed, with the SHA.
11. Only then begin minimal CNA regression cases, then fixes, in the order in
    section 5.
12. Do not begin cna-street visual work until the framework fixes are validated.
```

### Useful commands

```sh
# build and test
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCNA_ROOT_DIR=/home/user/cna
cmake --build build -j4
ctest --test-dir build --output-on-failure

# content, from scratch
rm -rf assets/content && cmake --build build --target content -j4
cmake --build build --target validate-assets

# the imported-rig round trip, logged every run
xvfb-run -a ./build/bin/cna-street --frames 1 --width 320 --height 200 --no-overlay 2>&1 | grep -i "imported rig"

# performance, measured properly
xvfb-run -a ./build/bin/cna-street --viewpoint 1 --width 1024 --height 576 --frames 26 --no-overlay

# inspect a compiled model
./build/bin/cna_tool_cnb_info assets/content/cesium-man.cnb
```

---

## 9. Where the rest of the knowledge lives

| Document | What it holds |
| --- | --- |
| `docs/cna-findings.md` | The full findings inventory — reproductions, workarounds, proposed fixes |
| `docs/visual-overhaul/report.md` | What the overhaul did, what it cost, the honest answer on quality |
| `docs/visual-overhaul/performance.md` | Frame-time methodology and every number quoted here |
| `docs/visual-overhaul/comparisons.md` | Before/after image pairs, and what each viewpoint exposes |
| `docs/visual-overhaul/references.md` | What the street is aimed at — material references, anthropometry |
| `docs/visual-overhaul/asset-review.md` | Every external asset, its licence, and why it is or is not used |
| `docs/design-notes.md` | The transferable lessons, including the PBR factor bug |
| `docs/patches/` | The CNA-F12 contribution as a patch |
| `assets/external/manifest.json` | Machine-readable per-asset licence record |
| `plan.md` | Project history and the deferred milestone |

---

## 10. Suggested prompt for the future CNA repair session

Copy this into a fresh Claude Code / Codex context once CNA is free.

---

> Another agent has finished working on CNA. You are working across `cna-street`
> (application) and `cna` (framework); `sharp-runtime` is read-only and needs no
> work.
>
> **Start by reading `docs/cna-followup-after-framework-work.md` in `cna-street`.**
> It is the authoritative handoff and contains a full issue inventory, the
> debugging already done, what has been ruled out, minimal reproducers and the
> required ordering. Do not begin coding before reading it.
>
> Then, in this order:
>
> 1. Confirm with me that CNA is genuinely free to modify. Record branch and HEAD
>    SHA for `cna-street`, `cna` and `sharp-runtime`. Do not reset or discard any
>    work you did not create.
> 2. Build `cna-street` unchanged and run its tests, to establish that the
>    application still works against the new CNA baseline.
> 3. **Revalidate every finding** in the handoff's issue inventory against that
>    baseline. `CNA-F1`, `CNA-F2` and `CNA-F3` already look fixed upstream and
>    `CNA-F12` was fixed by this project — confirm, and mark anything else that
>    has changed. Report what is still open before writing a fix.
> 4. Write **minimal failing regression tests in CNA** for what remains, then fix,
>    in this order:
>    * **`GLTF-208` (alias `CNASTREET-SKINDRAW`)** — an imported skinned mesh part
>      draws nothing. Start with the runtime-glTF-versus-compiled-CNB A/B
>      experiment in the handoff; it halves the search in one step. Note the
>      handoff has already ruled out winding, culling, the vertex declaration,
>      the palette, the material, the transform, 16-bit indices and the draw
>      parameters — do not repeat that work. Note also that CNA's own
>      `easygl_model_skinned_animation_playback_test` uses a `.model.json`
>      fixture with stock `SkinnedEffect`, so the glTF→`.cnb`→`SkinnedPbrEffect`
>      path has no coverage at all.
>    * **`GLTF-207` (alias `CNASTREET-SKINMETA`)** — a compiled `.cnb` leaves
>      `Model::SkinsEXT` empty and puts the skin on `Model::Tag`, while the
>      runtime glTF path populates `SkinsEXT`. Same code region; do it with the
>      above.
>    * **`CNA-F14`** — `ShadowMap` already has `getSkinnedCasterEffect()` and
>      `applySkinnedCaster()` (plan item `MOD-810`, marked done).
>      `CascadedShadowMap` does not expose them. Extend it.
>    * **`GLTF-206`** — role-aware mip generation for glTF-referenced textures,
>      reusing `GenerateRgba8MipChain` with the colour space chosen per material
>      slot.
>    * If cheap: `CNA-F6`, `CNA-F7`, `CNA-F8`, `CNA-F9`.
>
>    **Beware an ID collision:** `GLTF-207` and `GLTF-208` mean something else
>    entirely in CNA's own `plans/plan_gltf.md` (per-part sampler state). Only
>    `GLTF-206` matches.
> 5. Run CNA's relevant test suites.
> 6. Build `cna-street` against the fixed CNA **unchanged** and confirm nothing
>    regressed, before removing anything.
> 7. Remove the obsolete `cna-street` workarounds listed in the handoff's Phase 2,
>    one at a time, re-rendering the affected viewpoint after each.
> 8. Then continue with the post-CNA roadmap in the handoff — imported animated
>    pedestrian first, then vehicles, then vegetation, then reflections.
>
> **Do not** begin application visual work before the framework fixes are
> validated. **Do not** remove the procedural character, vehicle, prop or texture
> systems — they are fallbacks, LOD tiers and regression baselines. **Do not**
> work around a framework problem in the application when the fix belongs in the
> framework. Observe the anti-goals in section 7 of the handoff.

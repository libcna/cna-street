# Developing cna-street

This is the working guide for changes to the C++ application.

## Source map

| Path | Responsibility |
| --- | --- |
| `street/src/StreetApplication.cpp` | Startup, command line, input, frame loop and captures |
| `street/src/Scene/` | City layout, placements and scene assembly |
| `street/src/Geometry/` | Procedural mesh generation |
| `street/src/Props/` | Buildings, vehicles, people and street furniture |
| `street/src/Assets/` | Generated textures and imported models |
| `street/src/Render/` | Materials, sky, probes, culling, drawing and overlay |
| `street/src/Sim/` | Signals, traffic and pedestrians |
| `street/src/Audio/` | Sound playback |
| `street/src/Bench/` | Repeatable benchmark cameras and result formats |
| `street/include/CnaStreet/` | Public interfaces for the corresponding source folders |
| `tests/` | CTest executables for geometry, simulation, settings, assets and benchmarks |

The executable in `street/src/Main.cpp` is deliberately thin. Most application
code builds into `CnaStreet::Core`, which the tests also link.

## Build and run

CMake requires C++23 and a sibling CNA checkout. CNA resolves sharp-runtime,
easy-gl and meta-gl from its own sibling directories. Use
`scripts/fetch-dependencies.sh` for a fresh checkout, or set `CNA_ROOT_DIR` and
`CNA_SHARP_RUNTIME_ROOT` when those directories have different names. The
default renderer is `OPENGL33`; set `CNA_STREET_RENDERER` to select another
supported renderer.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build build --target cna-street --parallel
./build/bin/cna-street --help
./build/bin/cna-street --no-audio
```

If ccache is unavailable, omit the two launcher options. Reuse `build/` for
incremental work. Configure once for a new renderer set; repeated clean builds
of CNA are expensive. `CNA_GRAPHICS_RENDERERS` may contain several supported
renderers separated by semicolons, and `CNA_GRAPHICS_RENDERER` chooses one at
runtime.

The application loads `assets/config/render.json` when present. `--dump-settings`
prints effective settings, and `--settings <file>` loads an alternate file.
External models and scans are optional: the generated scene runs without them.

## Checks

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 scripts/validate-assets.py
```

For visual changes, capture a fixed view and inspect it:

```sh
SDL_VIDEODRIVER=offscreen ./build/bin/cna-street \
  --no-audio --screenshot /tmp/cna-street-check.png
```

`scripts/check-screenshots.sh` compares all 18 named views with
`docs/screenshots/`. It requires the `compare-images` tool and a working GL
display; use it for rendering or composition changes. Update the reference
images only after inspecting the new captures, since changing the baseline
changes what the check can catch. The source tree's screenshot references are
test inputs, while temporary captures belong outside `docs/`.

The benchmark command is `./build/bin/cna-street --benchmark-list` followed by
`--benchmark <preset>`. `scripts/benchmark.sh` runs the full preset set. Compare
runs at the same resolution, renderer and approximate machine load.

## Content and shaders

`assets/external/manifest.json` records sources, licences and checksums.
`scripts/fetch-assets.sh` downloads assets with public URLs;
`scripts/prepare-audio.py` handles separately obtained sound packs.
`validate_assets` checks the manifest and any downloaded files. Add attribution to
`assets/ATTRIBUTION.md` when adding a new external asset. `cmake --build build
--target content` bakes generated textures and compiles content; the app can
also generate surfaces at startup.

The sky shaders live in `street/src/Render/shaders/sky/`. After changing a
packaged shader, regenerate `SkyShaderPackage.generated.hpp` with CNA's shader
package generator using that folder's `package.json`, then build and inspect
at least one capture on each affected renderer.

## Documentation to maintain

Keep `README.md` focused on setup and use, this file on development, and
`assets/ATTRIBUTION.md` on provenance. The removed audit and comparison reports
remain available in Git history when a past decision needs investigation.

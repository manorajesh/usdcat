# usdcat

[![Build](https://github.com/manorajesh/usdcat/actions/workflows/build.yml/badge.svg)](https://github.com/manorajesh/usdcat/actions/workflows/build.yml)

A terminal-based USD (Universal Scene Description) viewer written in C++. Renders USD scenes as colored Unicode art directly in your terminal using Hydra's rendering pipeline.

## Features

- Hydra-based rendering pipeline (UsdImagingDelegate + custom HdRenderDelegate)
- Two render modes: **HalfBlock** (`▀`, default) and **Braille** (`⠿`)
- Multi-threaded triangle rasterization via horizontal bands
- USD Preview Surface material support: base color, textures, metallic, roughness, occlusion, emissive, opacity, and normal maps
- Camera-relative studio/environment lighting for quick object inspection
- Z-buffered triangle rendering with 24-bit ANSI color output
- Interactive camera: orbit, zoom, auto-frame
- Frame time / FPS display
- Raw terminal I/O (no ncurses dependency)

This is intended as a fast terminal debug viewer. It uses Hydra for USD scene ingestion, then shades and rasterizes with its own CPU renderer. It is not a pixel-match replacement for Storm, hdPrman, or Finder's full renderer.

## Dependencies

- CMake 3.12+
- C++17 compiler (AppleClang or GCC/Clang)
- [Eigen3](https://eigen.tuxfamily.org/)
- OpenUSD (`pxr`) — expects install at `/opt/usd` by default
- OpenGL (used by Hydra internals only; no window/GL context created)

## Building

Use the provided scripts (cargo-style):

```bash
# Debug build
./build.sh

# Release build
./build.sh --release

# RelWithDebInfo (default for CMake if not specified)
./build.sh --relwithdebinfo

# Clean and rebuild
./build.sh --clean
```

Or manually:

```bash
mkdir -p build/debug && cd build/debug
cmake -DCMAKE_BUILD_TYPE=Debug ../..
cmake --build . -j$(sysctl -n hw.ncpu)
```

If OpenUSD is installed somewhere other than `/opt/usd`, pass the path:

```bash
cmake -Dpxr_DIR=/path/to/usd ../..
```

### macOS note

`pxrConfig.cmake` bakes in the SDK version used when USD was built. If your Xcode SDK has been updated since USD was compiled, `CMakeLists.txt` automatically patches the imported target include paths to point to the current SDK. No manual intervention is needed.

## Usage

```bash
# Build and run (like cargo run)
./run.sh <scene.usd>

# With render mode flag
./run.sh <scene.usd> -hb    # HalfBlock (default)
./run.sh <scene.usd> -b     # Braille

# Examples
./run.sh Hubble.usdz
./run.sh Hubble.usdz -b
./run.sh Hubble.usdz -hb

# Release build
./run.sh --release <scene.usd>
./run.sh --release <scene.usd> -b
```

Or run the binary directly after building:

```bash
./build/debug/usdcat <scene.usd>
./build/debug/usdcat <scene.usd> -b
./build/release/usdcat <scene.usd> -b
```

### Render Modes

| Flag | Mode | Notes |
|------|------|-------|
| `-hb` | HalfBlock | Default. Uses upper half-block characters with separate foreground/background color per terminal cell. Good general-purpose mode. |
| `-b` | Braille | Uses Braille cells for finer geometric detail. Best with small terminal font sizes and true-color terminal support. |

Both modes emit 24-bit ANSI color. For best results, use a terminal with true-color support and a small font size when inspecting detailed assets.

Sample files included: `cube.usda`, `simple_primitives.usda`, `Hubble.usdz`, `easyChair_01.usdc`, `test_materials.usda`.

### Controls

| Key | Action |
|-----|--------|
| Arrow keys | Orbit camera |
| `w` / `s` | Zoom in / out |
| `f` | Frame all meshes |
| `q` or Ctrl+C | Quit |

## Materials and Lighting

`usdcat` reads Hydra material networks and approximates `UsdPreviewSurface` in the CPU renderer. Supported inputs include:

- `diffuseColor` / base color texture
- `metallic`
- `roughness`
- `normal` texture
- `occlusion` and occlusion texture
- `emissiveColor`
- `opacity`

Texture files are loaded with `HioImage`, including textures packaged inside `.usdz` files. The renderer also recognizes common USD texture coordinate primvars such as `st` and `st0`.

Lighting is a camera-relative studio setup designed for debugging:

- warm key light
- cool fill light
- rim light
- hemisphere ambient light
- view-dependent metallic/environment reflection

This keeps objects readable as you orbit and makes metallic assets react to camera movement. It is an approximation, not a complete physically based renderer.

### Known Limitations

- No shadows, transparency sorting, displacement, subdivision surface evaluation, skeletal animation, or instancing-specific material overrides yet.
- The PBR model is a lightweight PreviewSurface approximation, not full Hydra Storm shading.
- Normal mapping depends on generated tangent frames from positions and UVs, so unusual UV layouts can produce imperfect results.
- Very large scenes can be CPU-heavy, especially in Braille mode or with small terminal fonts.

## Architecture

```
main.cpp                 — entry point, render loop, signal handling
src/
  renderer.cpp           — framebuffer management, projection, CPU rasterization, PreviewSurface-style shading
  mesh.cpp               — HdTerminalMesh (Hydra Rprim): syncs USD geometry, UVs, normals, material bindings
  material.cpp           — HdTerminalMaterial (Hydra Sprim): parses material networks and loads textures via HioImage
  delegate.cpp           — HdTerminalDelegate (HdRenderDelegate): factory for prims
  render_pass.cpp        — HdTerminalRenderPass: calls renderer per Hydra execute
  camera_controller.cpp  — orbit camera math, input handling
  screen.cpp             — raw terminal I/O (ANSI escape codes, no ncurses)
  frame_timer.cpp        — moving-average FPS counter
include/
  render_task.h          — HdTerminalRenderTask: wires camera state into render pass
```

The rendering pipeline:

1. `UsdImagingDelegate` populates a `HdRenderIndex` from the USD stage.
2. On each frame, `HdEngine::Execute` calls `HdTerminalRenderPass::_Execute`.
3. Dirty `HdTerminalMesh` prims sync geometry, transforms, UVs, normals, and material bindings into the renderer.
4. Dirty `HdTerminalMaterial` prims parse PreviewSurface inputs and load texture assets.
5. `Renderer::update_framebuffer` pre-transforms triangles, rasterizes them in parallel horizontal bands, shades each covered sample, and writes a high-resolution color buffer.
6. Each terminal cell is encoded as a colored half-block or Braille character with 24-bit ANSI color.
7. The output string is written directly to stdout via the `Screen` class.

## Debug

macOS debug symbols (dSYM) are generated automatically on post-build. Verify UUIDs match before attaching a debugger or profiler:

```bash
dwarfdump --uuid ./build/debug/usdcat ./build/debug/usdcat.dSYM
```

Profile with Instruments by attaching to the running process.

## License

MIT

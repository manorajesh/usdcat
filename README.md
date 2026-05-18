# usdcat

[![Build](https://github.com/manorajesh/usdcat/actions/workflows/build.yml/badge.svg)](https://github.com/manorajesh/usdcat/actions/workflows/build.yml)

A terminal-based USD (Universal Scene Description) viewer written in C++. Renders USD scenes as colored Unicode art directly in your terminal using Hydra's rendering pipeline.

## Features

- Hydra-based rendering pipeline (UsdImagingDelegate + custom HdRenderDelegate)
- Two render modes: **HalfBlock** (▀ characters, default) and **Braille** (⠿ characters)
- SIMD-accelerated rasterization (NEON on ARM64, SSE on x86)
- Multi-threaded triangle rasterization via horizontal bands
- Diffuse texture support (reads from USD materials via `HioImage`)
- Lambert shading with z-buffering
- Interactive camera: orbit, zoom, auto-frame
- Frame time / FPS display
- Raw terminal I/O (no ncurses dependency)

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

# Release build
./run.sh --release <scene.usd>
```

Or run the binary directly after building:

```bash
./build/debug/usdcat <scene.usd>
./build/release/usdcat <scene.usd> -b
```

Sample files included: `cube.usda`, `simple_primitives.usda`, `Hubble.usdz`, `easyChair_01.usdc`, `test_materials.usda`.

### Controls

| Key | Action |
|-----|--------|
| Arrow keys | Orbit camera |
| `w` / `s` | Zoom in / out |
| `f` | Frame all meshes |
| `q` or Ctrl+C | Quit |

## Architecture

```
main.cpp                 — entry point, render loop, signal handling
src/
  renderer.cpp           — framebuffer management, projection, SIMD rasterization
  mesh.cpp               — HdTerminalMesh (Hydra Rprim): syncs USD geometry
  material.cpp           — HdTerminalMaterial (Hydra Sprim): loads textures via HioImage
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
3. The render pass iterates over all dirty `HdTerminalMesh` prims, which sync their geometry into `Renderer::meshes`.
4. `Renderer::update_framebuffer` pre-transforms all triangles, then rasterizes them in parallel horizontal bands using NEON/SSE/scalar SIMD.
5. Each terminal cell is encoded as a colored half-block or Braille character with 24-bit ANSI color.
6. The output string is written directly to stdout via the `Screen` class.

## Debug

macOS debug symbols (dSYM) are generated automatically on post-build. Verify UUIDs match before attaching a debugger or profiler:

```bash
dwarfdump --uuid ./build/debug/usdcat ./build/debug/usdcat.dSYM
```

Profile with Instruments by attaching to the running process.

## License

MIT

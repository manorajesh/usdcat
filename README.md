<div align="center">

# usdcat

[![Build](https://github.com/manorajesh/usdcat/actions/workflows/build.yml/badge.svg)](https://github.com/manorajesh/usdcat/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)

**A terminal-based USD (Universal Scene Description) viewer written in C++.**

<img src="https://placehold.co/800x400/11111b/cdd6f4.png?text=Showcase+Your+Terminal+Art+Here" alt="usdcat screenshot" width="600" style="border-radius: 8px;"/>

*Renders USD scenes as colored Unicode art directly in your terminal using Hydra's rendering pipeline.*

</div>

---

## ✨ Features

- **Hydra-Powered**: Ingests scenes via `UsdImagingDelegate` and renders them with a custom `HdRenderDelegate`.
- **Dual Render Modes**: Choose between **HalfBlock** (`▀`, default) and **Braille** (`⠿`) rendering styles.
- **Multithreaded**: Speedy CPU rasterization utilizing parallel horizontal bands.
- **PBR Approximation**: Extensive USD Preview Surface material support, including:
  - Base color and matching textures
  - Metallic & roughness properties
  - Occlusion, emissive, and opacity maps
  - Normal mapping
- **Studio Lighting**: Camera-relative lighting setup perfect for quick asset inspection.
- **Vibrant Output**: Z-buffered triangle rendering pushing rich **24-bit ANSI color**.
- **Interactive TUI**: Smooth camera controls to orbit, zoom, and auto-frame your mesh—complete with FPS/frame time displays.
- **Raw Terminal I/O**: Fast, dependency-free terminal rendering (no `ncurses` needed).

> [!NOTE]
> This project is designed as a fast, localized **terminal debug viewer**. It uses Hydra for USD scene ingestion and its own CPU renderer for shading. It is *not* a pixel-match replacement for high-fidelity renderers like Storm or hdPrman.

## 📦 Dependencies

To build `usdcat`, ensure you have the following installed:

- **CMake** 3.12+
- **C++17 Compiler** (AppleClang or GCC/Clang)
- **[Eigen3](https://eigen.tuxfamily.org/)**
- **OpenUSD** (`pxr`)
- **OpenGL** (utilized passively by Hydra internals; no window or GL context is created).

> [!IMPORTANT]
> The build system expects OpenUSD to be installed at `/opt/usd` by default. If your installation is located elsewhere, you'll need to specify its path during CMake configuration.

## 🚀 Building

You can quickly build `usdcat` using the provided Cargo-style helper scripts:

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

### Manual CMake Build

For more control, configure and build manually:

```bash
mkdir -p build/debug && cd build/debug
cmake -DCMAKE_BUILD_TYPE=Debug ../..
cmake --build . -j$(sysctl -n hw.ncpu)
```

> [!TIP]
> If OpenUSD is installed at a custom location, pass the path via `pxr_DIR`:
> ```bash
> cmake -Dpxr_DIR=/path/to/usd ../..
> ```

> [!NOTE]
> **macOS Note**: `pxrConfig.cmake` bakes in the SDK version used when USD was built. If your Xcode SDK has been updated since USD was compiled, `CMakeLists.txt` automatically patches the imported target include paths to point to the current SDK. No manual intervention is needed!

## 🎮 Usage

Run a USD scene simply by invoking the launcher script:

```bash
# General invocation (HalfBlock mode by default)
./run.sh <scene.usda/usdc/usdz>

# Alternatively, pick your render mode
./run.sh <scene.usd> -hb    # HalfBlock
./run.sh <scene.usd> -b     # Braille
```

**Examples:**
```bash
./run.sh Hubble.usdz
./run.sh Hubble.usdz -b
./run.sh --release Hubble.usdz
```

Or just run the compiled binary directly:
```bash
./build/release/usdcat Hubble.usdz -hb
```

### 🔲 Render Modes

| Flag | Mode | Description |
|------|------|-------------|
| `-hb` | **HalfBlock** | **Default**. Uses upper half-block characters with a separate foreground/background color per cell. This is the **best general-purpose mode**. |
| `-b` | **Braille** | Uses Braille cells for capturing fine geometric detail. *For best results, use small terminal fonts and ensure true-color terminal support.* |

> [!TIP]
> For the absolute best viewer experience, make sure your terminal supports **24-bit True Color** and scale your font size down when inspecting detailed, asset-heavy scenes using Braille mode.

You can try out the viewer using the sample files included in the project:  
`cube.usda`, `simple_primitives.usda`, `Hubble.usdz`, `easyChair_01.usdc`, `test_materials.usda`.

### 🕹️ Controls

| Key Bindings | Action |
|--------------|--------|
| `🡐` `🡒` `🡑` `🡓` | Orbit camera around center |
| `w` / `s` | Zoom camera in / out |
| `f` | Auto-frame all visible meshes |
| `q` or `Ctrl+C` | Quit viewer |

## 🎨 Materials & Lighting

`usdcat` is capable of reading Hydra material networks to construct a lightweight CPU approximation of `UsdPreviewSurface`. Supported inputs include:

- **Albedo:** `diffuseColor` / base color textures
- **PBR Parameters:** `metallic`, `roughness`, `occlusion` (and maps)
- **Detail:** `normal` maps
- **Masking & Glow:** `opacity` / `emissiveColor`

Textures are fetched and mapped using `HioImage`—meaning packaged `.usdz` assets work straight out of the box! Common texture coordinates (like `st` and `st0`) are automatically read.

### The Lighting Model

Because `usdcat` is designed for quick terminal debugging, it uses a **camera-relative studio lighting setup**, featuring:
- A warm key light
- A cool fill & rim light
- Ambient hemisphere grounding
- Sleek view-dependent environment/metallic reflections

This keeps models readable and dynamic as you orbit.

> [!WARNING]
> **Known Limitations**
> - The PBR engine is an approximation and will not perfectly match Hydra Storm shading.
> - Normal mapping heavily relies on tangent frames generated from positions/UVs; atypical UV layouts may fragment.
> - Very large scenes are CPU-intensive. Limit Braille mode and scale up terminal text size if you experience major lag.
> - Current feature set lacks shadows, displacement, sub-surface modeling, skeletal animation, instancing overrides, and transparency sorting.

## 🏗️ Architecture Stack

The project relies on a clean, scalable separation of duties bridging Hydra, the camera, and the TUI:

```text
usdcat
├── main.cpp                 — Entry point, event loop, terminal state management
├── src/
│   ├── renderer.cpp         — Framebuffer management, projection, rasterization & PBR shading
│   ├── mesh.cpp             — (HdTerminalMesh) Syncs Hydra Rprim topology, UVs, normals & materials
│   ├── material.cpp         — (HdTerminalMaterial) Parses network and routes HioImage assets
│   ├── delegate.cpp         — (HdRenderDelegate) Initializes the terminal rendering Hydra factory
│   ├── render_pass.cpp      — Fires renderer updates triggered per Hydra execute call
│   ├── camera_controller.cpp— Orbit math, quaternion tracking, input routing
│   ├── screen.cpp           — Direct raw ANSI VT100 sequence buffering wrapper
│   └── frame_timer.cpp      — Smooth moving-average FPS telemetry
└── include/                 — Core header declarations
```

**The Render Pipeline summary:**
1. `UsdImagingDelegate` unpacks the USD stage into an `HdRenderIndex`.
2. On every render tick, `HdEngine::Execute` loops `HdTerminalRenderPass::_Execute`.
3. Geometries (`HdTerminalMesh`) sync point and topology changes to the custom renderer.
4. Active materials (`HdTerminalMaterial`) analyze incoming parameters and flush memory mapping.
5. `Renderer::update_framebuffer` pre-transforms vertices, invokes parallel band-rendering threads, and interpolates colors per-pixel.
6. Target cells represent rendering units pushed as ANSI blocks—which are dumped in chunks via `stdout`.

## 🐛 Debugging & Profiling

macOS debug symbols (`.dSYM`) are automatically created in post-build steps. Before attaching external debuggers/profilers, you can verify your build components match UUIDs:

```bash
dwarfdump --uuid ./build/debug/usdcat ./build/debug/usdcat.dSYM
```

To profile bottlenecks, attach **Instruments** directly to the running application component.

## 📜 License

This project is licensed under the **[MIT License](LICENSE)**.

---
<div align="center">
  <i>ഹലോ • नमस्कार</i>
</div>

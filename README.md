# usdcat

[![Build](https://github.com/manorajesh/usdcat/actions/workflows/build.yml/badge.svg)](https://github.com/manorajesh/usdcat/actions/workflows/build.yml)

A lightweight C++ terminal-based viewer for USD (Universal Scene Description) scenes. Renders USD files as ASCII/Unicode art directly in your terminal.

## Features

- Fast terminal rendering using ncurses
- Real-time 3D visualization in ASCII/Unicode
- Interactive camera controls
- Minimal dependencies

## Dependencies

- CMake 3.10+
- C++17 compiler
- Eigen3
- ncurses
- USD (Universal Scene Description)

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./build/usdcat <scene.usd>
```

## Debug

To enable debugging with symbol information on MacOS, ensure that the debug symbols in the executable and the dSYM bundle match. You can verify this using the following commands:

```bash
# generate one if missing
dsymutil ./usdcat -o ./usdcat.dSYM

# verify UUIDs match (they MUST match)
dwarfdump --uuid ./usdcat ./usdcat.dSYM
```

Profile in Instruments for example by attaching to a running process.

### Controls

- Arrow keys: Rotate camera
- `w`/`s`: Zoom in/out
- `q`: Quit

## License

MIT

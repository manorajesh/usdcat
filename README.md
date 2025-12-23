# usdcat

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

### Controls

- Arrow keys: Rotate camera
- `w`/`s`: Zoom in/out
- `q`: Quit

## License

MIT

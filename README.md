## Komal

Komal is an open-source, cross-platform raster graphics editor written in **C++**.

The application uses **Qt 6** for its interface and [**wgpu-native**](https://github.com/gfx-rs/wgpu-native) for GPU-accelerated rendering, with the goal of providing a modern and non-destructive graphics editing software.

> [!WARNING]
> Komal is currently in active development and is **not yet ready for production use**.
## Features

- Cross-platform rendering through **WebGPU** (currently Windows and Linux)
- Infinite canvas using dynamic chunk-based raster storage
- Node-based raster compositing
- GPU texture atlas rendering
- Per-tile invalidation and caching
- Layer bounds tracking
- Canvas panning and cursor-anchored zooming
- Basic raster drawing and erasing
## Architecture

Komal separates raster data, compositing, and GPU rendering into distinct systems:

```
RasterData
    ↓
RasterRootNode
    ↓
CompositorNode
    ↓
Tile Cache
    ↓
Texture Atlas
    ↓
WebGPU
    ↓
Canvas
```

### Raster data

Raster images are stored as collections of dynamically allocated **64×64 pixel chunks** rather than as one large contiguous image.

Only chunks that are actually used are allocated. This allows the canvas to exist in world space without requiring the entire canvas to be allocated up front.

Individual chunks track their own bounds and dirty state, allowing changes to propagate only to the parts of the image that were modified.

### Node system

Raster data is exposed to the compositor through a small node abstraction.

`RasterRootNode` provides access to raster data, while `CompositorNode` combines multiple nodes into a final image.

The compositor currently supports:

- Layer visibility
- Layer opacity
- Layer ordering
- Normal blending
- Multiply
- Add
- Screen

Nodes maintain cached tiles and invalidate only the affected regions when their inputs change.

### GPU rendering

The composited raster output is uploaded to the GPU in **64×64 tiles**.

Rather than creating an individual GPU texture for every tile, Komal packs tiles into larger **texture atlas pages**. Each chunk keeps a slot inside an atlas and is updated in-place when its raster data changes.

This allows only modified regions of the canvas to be synchronized with the GPU, avoiding full texture uploads whenever possible.

## Requirements

- CMake 3.16+
- C++17 compiler
- Qt 6 (Widgets)

## Building

1. Clone the repository:
```bash
git clone https://github.com/tudorblank/komal.git
cd komal
```

2. Generate the build files:
### Windows
```bash
cmake --preset windows
cmake --build --preset windows
```
### Linux
```bash
cmake --preset linux
cmake --build --preset linux
```

The compiled executable will be available inside the generated `build` directory.

## To-do ☑️ [development]

- [ ] Add `Move` node
- [ ] GPU-based `Blur` node
- [ ] Flesh out the UI
- [ ] Import / export support
- [ ] Artboard system
- [ ] Additional canvas interaction tools
- [ ] Undo / redo
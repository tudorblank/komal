## Komal

Komal is an open-source, cross-platform raster graphics editor written in **C++**.

The application uses [**ImGui**](https://github.com/ocornut/imgui) for its interface and [**wgpu-native**](https://github.com/gfx-rs/wgpu-native) for GPU-accelerated rendering, with the goal of providing a modern and non-destructive graphics editing software.

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

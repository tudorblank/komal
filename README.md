<p align="center">
  <img src="assets/icon/komal-icon.png" width="128" height="128" alt="Komal icon">
</p>

<h1 align="center">Komal</h1>

<p align="center">
  <b>A node-based, non-destructive raster editor written in C++.</b><br>
  Paint on an infinite canvas, wire edits together in a graph, change your mind later.
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599c?style=flat-square&logo=cplusplus">
  <img alt="Windows · macOS · Linux" src="https://img.shields.io/badge/Windows%20%C2%B7%20macOS%20%C2%B7%20Linux-native-2f7bf5?style=flat-square">
  <img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-3a3a3a?style=flat-square">
  <img alt="Status: early development" src="https://img.shields.io/badge/status-early%20development-d69e2e?style=flat-square">
</p>

> [!WARNING]
> Komal is in active development and isn't ready for real work yet. Things will move, break and get renamed.

## What it does

- **Infinite canvas.** Pixels live in 64×64 tiles that only exist where you've painted, so the canvas has no edges.
- **Node graph.** Raster, Reference, Move and Compositor nodes wired together. Every edit stays live, so you can tweak an offset or a mix long after you made it.
- **History tree.** Undo and redo never lose work: going back and making a new change starts a branch, and you can jump to any step of any branch.
- **GPU everywhere.** Rendering runs on WebGPU via wgpu-native (Vulkan, Metal, DX12). Tiles are drawn from texture atlases, with levels of detail when you zoom out.
- **Live previews.** Dragging a node's value previews the result on the GPU at full resolution, then commits when you let go.
- **Painting.** A brush with size, hardness, opacity, flow and spacing, pen pressure, an eraser, and primary/secondary colors (<kbd>X</kbd> swaps them).
- **Image import.** Open images from File > Import, or drop them onto the canvas or the graph.
- **Themes.** The colors come from theme files (`assets/themes/*.toml`). Main Dark is the default; start with `--theme main-light` to try the light draft.
- **Gets out of the way.** A dockable UI, cursor-anchored zoom, <kbd>F</kbd> to frame everything, and an idle loop that sits at ~0% CPU when nothing is happening.

## Building

You'll need CMake 3.25+, Ninja and a C++20 compiler. Everything else is downloaded at configure time, pinned and checked by its hash. `-DKOMAL_SYSTEM_DEPS=ON` uses an installed SDL3 and abseil instead.

```sh
# Linux
cmake --preset linux-release
cmake --build --preset linux-release
./build/linux-release/bin/Komal

# macOS
cmake --preset macos-release
cmake --build --preset macos-release

# Windows (Visual Studio 2022)
cmake --preset windows-msvc-x64
cmake --build --preset windows-msvc-x64-release
```

Run it with `--theme <name>` to pick a theme, and any image paths to import them at startup.

There are `-debug` variants of each (warnings are errors there), `linux-asan` for AddressSanitizer and UBSan, plus `windows-release` for cross-compiling from Linux with mingw-w64. On Linux, `cmake --install` puts the binary, a `.desktop` file and the icon in the usual places.

### Tests

Two test suites are built and run with every build, and a failing test fails the build: `komal-tests` for the core (everything below the GPU and the UI), and `komal-ui-tests` for the UI's look (theme files, the palette, the styles). To run them by hand: `./build/linux-release/bin/komal-tests` and `./build/linux-release/bin/komal-ui-tests`.

## Layout

The source folders are layers, lowest first. A folder only includes the ones below it, and the build checks this before every compile (`cmake/check-layers.cmake`).

| Folder | What's in it |
|---|---|
| `src/base` | Colors, math, clocks, logging, files: the standard library only |
| `src/input` | What input is (one frame of it, a press followed to its release), and SDL turned into it |
| `src/raster` | Pixels, tiles, tile grids, brush strokes |
| `src/node` | The node graph: nodes, signals, masks, rendering |
| `src/command` | The actions that input and menus trigger |
| `src/document` | The project: nodes, rasters, history, the document's actions |
| `src/gfx` | The GPU: device, buffers, textures, render passes |
| `src/ui` | The interface: its look (`style`), drawing (`render`), controls (`widgets`), panels and tools |
| `src/app` | The window, the main loop, startup |
| `assets` | Fonts, icons and themes, baked into the binary |
| `tests` | The core's tests; `tests/ui` the UI's |

## Credits

Komal is built on top of some excellent open-source work:

| Project | What it's used for | License |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | The whole UI | MIT |
| [Dear ImGui, docking branch](https://github.com/ocornut/imgui/tree/docking) | Dockable panels and layouts | MIT |
| [SDL3](https://github.com/libsdl-org/SDL) | Windows, input, events | zlib |
| [wgpu-native](https://github.com/gfx-rs/wgpu-native) | WebGPU rendering | MIT / Apache-2.0 |
| [Abseil](https://github.com/abseil/abseil-cpp) | Hash containers for tiles | Apache-2.0 |
| [stb](https://github.com/nothings/stb) | Image loading (`stb_image`) | MIT / Public domain |
| [toml++](https://github.com/marzer/tomlplusplus) | Reading theme files | MIT |
| [doctest](https://github.com/doctest/doctest) | The tests | MIT |

Fonts:

| Font | Used for | License |
|---|---|---|
| [Manrope](https://github.com/sharanda/manrope) by Mikhail Sharanda | UI text | SIL OFL 1.1 |
| [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) by JetBrains | Number fields | SIL OFL 1.1 |

## License

Komal's source code is licensed under the [GNU General Public License v3.0](LICENSE).

The **Komal Studio name and logo** are the only exceptions. They are not covered by the GPL, are copyrighted, and all rights to them are reserved. If you fork Komal, please give your version its own name and icon.

Third-party libraries and fonts keep their own licenses, listed above.

#!/usr/bin/env python3
# The source folders are layers, bottom first. A file may include its own folder and the folders
# below it, never one above: each layer is built only from what's under it. The build runs this
# (CMakeLists.txt) and stops on the first upward include, listing every one.
#
#   base       Signal, Deadline, Log, Perf, math, palette: no dependencies
#   raster     pixels, tiles, grids, rasters, brush strokes
#   node       the node graph: signals, masks, the render path, change propagation
#   command    input and the actions it triggers (the one way in, and the log)
#   document   the Project: nodes, rasters, history, the document's actions
#   gfx        the GPU: device, objects, pages of slots
#   canvas     the canvas engine: camera, scene, views, items, tiles, thumbnails
#   graph      the graph engine: scene, items, layout, painting
#   ui         widgets, palette use, panels, tools
#   app        the window, the loop, frames; main
#
#   check-layers.py <src dir>

import re
import sys
from pathlib import Path

LAYERS = ["base", "raster", "node", "command", "document", "gfx", "canvas", "graph", "ui", "app"]
RANK = {name: i for i, name in enumerate(LAYERS)}
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def main() -> int:
    src = Path(sys.argv[1])
    problems = []
    for path in sorted(src.rglob("*")):
        if path.suffix not in (".hpp", ".cpp"):
            continue
        layer = path.relative_to(src).parts[0]
        if layer not in RANK:
            problems.append(f"{path.relative_to(src)}: folder '{layer}' isn't a layer (add it to cmake/check-layers.py)")
            continue
        for included in INCLUDE.findall(path.read_text(encoding="utf-8")):
            target = included.split("/")[0] if "/" in included else layer   # "x.hpp": its own folder
            if target in RANK and RANK[target] > RANK[layer]:
                problems.append(f"{path.relative_to(src)}: includes \"{included}\" ({target} is above {layer})")
    for p in problems:
        print(f"layer check: {p}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

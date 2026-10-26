#!/usr/bin/env python3
# Builds the platform icon files from assets/komal-icon.png (needs Pillow: pip install pillow).
# Run it again whenever the PNG changes; the outputs are committed, so building needs nothing.
#   assets/komal-icon.ico       Windows: the .exe's icon (Explorer, taskbar), 16..256 px
#   assets/komal-icon.icns      macOS: the app bundle's icon (Finder, Dock), 16..1024 px
#   assets/komal-icon-256.png   every platform: the window icon set at startup, and Linux's
#                               desktop entry icon
import pathlib
from PIL import Image

assets = pathlib.Path(__file__).resolve().parent.parent / "assets"
src = Image.open(assets / "komal-icon.png").convert("RGBA")

def scaled(size):
    return src.resize((size, size), Image.LANCZOS)

scaled(256).save(assets / "komal-icon-256.png", optimize=True)
scaled(256).save(assets / "komal-icon.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
scaled(1024).save(assets / "komal-icon.icns")
print("icons written to", assets)

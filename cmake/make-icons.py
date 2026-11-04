#!/usr/bin/env python3
# Builds the platform icon files from the sources in assets/icons (needs Pillow: pip install pillow).
# Run it again whenever a source PNG changes; the outputs are committed, so building needs nothing.
#
# The app, from komal-icon.png:
#   komal-icon.ico       Windows: the .exe's icon (Explorer, taskbar), 16..256 px
#   komal-icon.icns      macOS: the app bundle's icon (Finder, Dock), 16..1024 px
#   komal-icon-256.png   every platform: the window icon set at startup, and Linux's desktop entry icon
#
# A .komal document, from doc-icon.png (not square: centred on a transparent square, its height):
#   doc-icon.ico         Windows: the document type's icon, in the .exe, 16..256 px
#   doc-icon.icns        macOS: the document type's icon, in the app bundle, 16..1024 px
#   doc-icon-<n>.png     Linux: the MIME type's icon (application-x-komal), n = 16 32 48 64 128 256 512
import pathlib
from PIL import Image

icons = pathlib.Path(__file__).resolve().parent.parent / "assets" / "icons"
DOC_PNG_SIZES = (16, 32, 48, 64, 128, 256, 512)

def squared(img):
    side = max(img.size)
    out = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    out.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    return out

def scaled(img, size):
    return img.resize((size, size), Image.LANCZOS)

app = Image.open(icons / "komal-icon.png").convert("RGBA")
scaled(app, 256).save(icons / "komal-icon-256.png", optimize=True)
scaled(app, 256).save(icons / "komal-icon.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
scaled(app, 1024).save(icons / "komal-icon.icns")

doc = squared(Image.open(icons / "doc-icon.png").convert("RGBA"))
for size in DOC_PNG_SIZES:
    scaled(doc, size).save(icons / f"doc-icon-{size}.png", optimize=True)
scaled(doc, 256).save(icons / "doc-icon.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
scaled(doc, 1024).save(icons / "doc-icon.icns")
print("icons written to", icons)

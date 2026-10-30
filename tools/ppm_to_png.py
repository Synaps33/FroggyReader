#!/usr/bin/env python3
import sys
from pathlib import Path
from PIL import Image

dirs = sys.argv[1:] or ["screenshots"]
for d in dirs:
    p = Path(d)
    for f in sorted(p.glob("*.ppm")):
        out = f.with_suffix(".png")
        Image.open(f).save(out)
        print(f"{f} -> {out}")

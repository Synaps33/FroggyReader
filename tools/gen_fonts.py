#!/usr/bin/env python3
"""Generate compact 4-bit bitmap atlases for book text and the UI.

The device has no FPU and no FreeType/TrueType renderer, so both families are
pre-rasterised on the build host. Book content uses DejaVu Serif Condensed;
the interface uses DejaVu Sans Condensed to get a clean Kindle-like UI. Japanese
UI characters are rendered from VL Gothic in both families.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
BODY_OUT = ROOT / "src" / "fonts_data.c"
UI_OUT = ROOT / "src" / "fonts_ui_data.c"

SERIF_DIR = Path("/usr/share/fonts/truetype/dejavu")
SANS_DIR = Path("/usr/share/fonts/truetype/dejavu")
JA_FONT = Path("/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf")
LANG_C = ROOT / "src" / "lang.c"

BODY_STYLES = [
    ("r", "DejaVuSerifCondensed.ttf", 0, 0),
    ("b", "DejaVuSerifCondensed-Bold.ttf", 1, 0),
    ("i", "DejaVuSerifCondensed-Italic.ttf", 0, 1),
    ("z", "DejaVuSerifCondensed-BoldItalic.ttf", 1, 1),
]
BODY_SIZES = [12, 15, 19, 24]

UI_STYLES = [
    ("r", "DejaVuSansCondensed.ttf", 0, 0),
    ("b", "DejaVuSansCondensed-Bold.ttf", 1, 0),
]
UI_SIZES = [12, 15, 19]

# Codepoint ranges for Latin, Greek, Cyrillic, typographic punctuation,
# symbols and ligatures. Japanese UI/kana codepoints are added below.
RANGES = [
    (0x0020, 0x007E),
    (0x00A0, 0x017F),
    (0x0180, 0x024F),
    (0x0370, 0x03FF),
    (0x0400, 0x045F),
    (0x0490, 0x04FF),
    (0x1E00, 0x1EFF),
    (0x2000, 0x203F),
    (0x2044, 0x2044),
    (0x20AC, 0x20AC),
    (0x2116, 0x2116),
    (0x2122, 0x2122),
    (0x2190, 0x2193),
    (0x2212, 0x2212),
    (0x2260, 0x2260),
    (0x2264, 0x2265),
    (0x25A0, 0x25FF),
    (0xFB00, 0xFB06),
    (0xFFFD, 0xFFFD),
]


def japanese_codepoints():
    chars = set(range(0x3000, 0x3100))
    if LANG_C.exists():
        text = LANG_C.read_text(encoding="utf-8")
        for ch in text:
            cp = ord(ch)
            if (0x3400 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF
                    or 0xFE30 <= cp <= 0xFE4F):
                chars.add(cp)
    return chars


def pack_nibbles(values):
    out = bytearray()
    for i in range(0, len(values), 2):
        hi = values[i] & 0xF
        lo = values[i + 1] & 0xF if i + 1 < len(values) else 0
        out.append((lo << 4) | hi)
    return out


def generate_family(styles, sizes, font_dir, ja_cps, all_cps, symbol_prefix):
    faces = []
    total_bitmap = 0
    for size in sizes:
        for style, fname, bold, italic in styles:
            tt = ImageFont.truetype(str(font_dir / fname), size)
            ja_tt = ImageFont.truetype(str(JA_FONT), size)
            ascent, descent = tt.getmetrics()
            glyphs = []
            bitmap = bytearray()
            for cp in all_cps:
                ch = chr(cp)
                cf = ja_tt if cp in ja_cps else tt
                try:
                    adv = int(round(cf.getlength(ch)))
                except Exception:
                    adv = 0
                if cp in (0x20, 0xA0):
                    glyphs.append((cp, 0, 0, 0, 0, max(1, adv)))
                    continue
                try:
                    bbox = cf.getbbox(ch, anchor="ls")
                except Exception:
                    bbox = None
                if not bbox:
                    if adv > 0:
                        glyphs.append((cp, 0, 0, 0, 0, max(1, adv)))
                    continue
                x0, y0, x1, y1 = bbox
                w = x1 - x0
                h = y1 - y0
                if w <= 0 or h <= 0:
                    if adv > 0:
                        glyphs.append((cp, 0, 0, 0, 0, max(1, adv)))
                    continue
                if adv <= 0 and w <= 1:
                    continue
                img = Image.new("L", (w, h), 0)
                dr = ImageDraw.Draw(img)
                dr.text((-x0, -y0), ch, font=cf, fill=255, anchor="ls")
                px = list(img.getdata())
                if max(px) == 0 and adv <= 0:
                    continue
                nib = [min(15, (p + 8) // 17) for p in px]
                off = len(bitmap)
                bitmap.extend(pack_nibbles(nib))
                glyphs.append((cp, w, h, x0, y0, max(1, adv), off))
            glyphs.sort(key=lambda g: g[0])
            line_h = int(round(size * 1.30))
            faces.append({
                "symbol": f"{symbol_prefix}{size}{style}",
                "name": f"{size}{style}",
                "size": size, "style": style, "bold": bold, "italic": italic,
                "ascent": ascent, "descent": descent, "line_h": line_h,
                "glyphs": glyphs, "bitmap": bytes(bitmap),
            })
            total_bitmap += len(bitmap)
            print(f"{symbol_prefix}{size:2d}{style}: {len(glyphs):4d} glyphs, {len(bitmap):6d} bytes")
    return faces, total_bitmap


def write_family(faces, out, array_name, count_macro, description):
    lines = []
    w = lines.append
    w(f"/* Generated by tools/gen_fonts.py -- {description}. Do not edit. */")
    w('#include "fonts.h"')
    w("")
    for f in faces:
        p = f["symbol"]
        w(f"static const EgGlyph {p}_glyphs[] = {{")
        for g in f["glyphs"]:
            if len(g) == 6:
                cp, gw, gh, gx, gy, adv = g
                off = 0
            else:
                cp, gw, gh, gx, gy, adv, off = g
            w(f"  {{{cp:#06x},{gw},{gh},{gx},{gy},{adv},{off}}},")
        w("};")
        w(f"static const uint8_t {p}_bitmap[] = {{")
        bm = f["bitmap"]
        for i in range(0, len(bm), 24):
            w("  " + ",".join(f"0x{b:02x}" for b in bm[i:i+24]) + ",")
        if not bm:
            w("  0")
        w("};")
        w("")
    w(f"const EgFont {array_name}[{count_macro}] = {{")
    for f in faces:
        p = f["symbol"]
        w(f'  {{"{f["name"]}", {p}_glyphs, '
          f'(int)(sizeof({p}_glyphs)/sizeof({p}_glyphs[0])), '
          f'{p}_bitmap, {len(f["bitmap"])}, {f["size"]}, '
          f'{f["ascent"]}, {f["descent"]}, {f["line_h"]}, '
          f'{f["bold"]}, {f["italic"]}}},')
    w("};")
    w("")
    out.write_text("\n".join(lines) + "\n")
    print(f"Wrote {out}")


def main():
    ja_cps = japanese_codepoints()
    cps = set()
    for lo, hi in RANGES:
        cps.update(range(lo, hi + 1))
    cps.update(ja_cps)
    all_cps = sorted(cps)

    body, body_bytes = generate_family(BODY_STYLES, BODY_SIZES, SERIF_DIR,
                                       ja_cps, all_cps, "f")
    write_family(body, BODY_OUT, "eg_fonts", "EG_FONT_COUNT", "book faces")
    ui, ui_bytes = generate_family(UI_STYLES, UI_SIZES, SANS_DIR,
                                   ja_cps, all_cps, "u")
    write_family(ui, UI_OUT, "eg_ui_fonts", "EG_UI_FONT_COUNT", "UI sans faces")
    print(f"Total book bitmap: {body_bytes} bytes")
    print(f"Total UI bitmap:   {ui_bytes} bytes")


if __name__ == "__main__":
    main()

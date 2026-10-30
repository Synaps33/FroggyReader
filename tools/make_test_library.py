#!/usr/bin/env python3
"""Create a deterministic, richly formatted EPUB test library.

Everything is generated locally (no copyrighted material) and exercises the
reader: CSS classes, inline styles, nested lists, tables, blockquotes, images
(JPEG/PNG/GIF/BMP), footnotes, poem layout, page breaks and EPUB2/EPUB3 TOCs.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED, ZIP_DEFLATED
from html import escape
import random
import math

from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "library"
SERIF = "/usr/share/fonts/truetype/freefont/FreeSerif.ttf"
SERIF_B = "/usr/share/fonts/truetype/freefont/FreeSerifBold.ttf"
SERIF_I = "/usr/share/fonts/truetype/freefont/FreeSerifItalic.ttf"


def font(size, bold=False, italic=False):
    if bold:
        return ImageFont.truetype(SERIF_B, size)
    if italic:
        return ImageFont.truetype(SERIF_I, size)
    return ImageFont.truetype(SERIF, size)


def wrap(draw, text, fnt, width):
    words = text.split()
    lines, cur = [], ""
    for w in words:
        test = (cur + " " + w).strip()
        if draw.textlength(test, font=fnt) <= width:
            cur = test
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def cover(path, title, author, c1, c2, accent, motif, subtitle=None):
    W, H = 480, 640
    img = Image.new("RGB", (W, H), c1)
    px = img.load()
    for y in range(H):
        t = y / H
        r = int(c1[0] * (1 - t) + c2[0] * t)
        g = int(c1[1] * (1 - t) + c2[1] * t)
        b = int(c1[2] * (1 - t) + c2[2] * t)
        for x in range(W):
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img, "RGBA")
    # subtle paper/leather grain
    rnd = random.Random(title)
    for _ in range(9000):
        x, y = rnd.randrange(W), rnd.randrange(H)
        v = rnd.randrange(-14, 15)
        r, g, b = px[x, y]
        px[x, y] = (max(0, min(255, r + v)), max(0, min(255, g + v)), max(0, min(255, b + v)))
    # ornamental geometry
    if motif == "arch":
        d.rectangle([46, 74, W - 46, H - 74], outline=(*accent, 190), width=2)
        d.arc([92, 120, W - 92, 360], 180, 360, fill=(*accent, 210), width=3)
        for i in range(5):
            x = 98 + i * 71
            d.line([x, 260, x, 500], fill=(*accent, 65), width=1)
    elif motif == "orbit":
        for rr in (70, 118, 166):
            d.ellipse([W // 2 - rr, 250 - rr, W // 2 + rr, 250 + rr], outline=(*accent, 100), width=2)
        d.ellipse([W // 2 - 6, 244, W // 2 + 6, 256], fill=(*accent, 230))
        for ang in (20, 120, 240):
            x = W // 2 + int(166 * math.cos(math.radians(ang)))
            y = 250 + int(166 * math.sin(math.radians(ang)))
            d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(*accent, 230))
    elif motif == "wave":
        for k in range(7):
            yy = 245 + k * 13
            pts = []
            for x in range(52, W - 52, 8):
                pts.append((x, yy + int(10 * math.sin(x / 27.0 + k * 0.55))))
            d.line(pts, fill=(*accent, 105), width=2)
    elif motif == "leaf":
        d.ellipse([130, 185, 350, 390], outline=(*accent, 130), width=3)
        d.line([240, 390, 240, 170], fill=(*accent, 190), width=3)
        for i in range(5):
            y = 225 + i * 30
            d.arc([160 - i * 4, y - 34, 240, y + 10], 275, 85, fill=(*accent, 170), width=2)
            d.arc([240, y - 34, 320 + i * 4, y + 10], 95, 265, fill=(*accent, 170), width=2)
    elif motif == "tower":
        d.rectangle([190, 160, 290, 470], outline=(*accent, 165), width=3)
        d.polygon([(175, 160), (240, 92), (305, 160)], outline=(*accent, 200), width=3)
        for y in range(205, 450, 50):
            d.rectangle([213, y, 267, y + 24], outline=(*accent, 125), width=2)

    # title block
    f_title = font(46, bold=True)
    f_sub = font(23, italic=True)
    f_author = font(24)
    lines = wrap(d, title, f_title, W - 96)
    y = 470 - len(lines) * 53
    for line in lines:
        tw = d.textlength(line, font=f_title)
        d.text(((W - tw) / 2 + 2, y + 2), line, font=f_title, fill=(0, 0, 0, 115))
        d.text(((W - tw) / 2, y), line, font=f_title, fill=(250, 242, 216, 255))
        y += 53
    if subtitle:
        sw = d.textlength(subtitle, font=f_sub)
        d.text(((W - sw) / 2, y + 2), subtitle, font=f_sub, fill=(*accent, 230))
        y += 38
    line_w = 118
    d.line([(W - line_w) / 2, y + 18, (W + line_w) / 2, y + 18], fill=(*accent, 220), width=2)
    aw = d.textlength(author, font=f_author)
    d.text(((W - aw) / 2, y + 32), author, font=f_author, fill=(232, 221, 190, 245))
    # outer frame
    d.rectangle([18, 18, W - 18, H - 18], outline=(*accent, 90), width=1)
    img.save(path, "JPEG", quality=88)


def illustration(path, kind, fmt=None, size=(640, 360)):
    W, H = size
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    rnd = random.Random(kind)
    if kind == "map":
        bg = (225, 205, 158, 255)
        img.paste(bg, [0, 0, W, H])
        for _ in range(10):
            pts = []
            x, y = rnd.randrange(40, W - 40), rnd.randrange(40, H - 40)
            for _ in range(5):
                pts.append((x, y))
                x += rnd.randrange(-120, 120)
                y += rnd.randrange(-80, 80)
            d.line(pts, fill=(103, 82, 51, 155), width=1)
        for _ in range(70):
            x, y = rnd.randrange(W), rnd.randrange(H)
            d.line([x, y, x + rnd.randrange(-10, 10), y + rnd.randrange(-8, 8)], fill=(130, 105, 66, 110))
        d.ellipse([W - 130, 30, W - 30, 130], outline=(92, 69, 42, 210), width=3)
        d.polygon([(W - 80, 48), (W - 66, 92), (W - 80, 112), (W - 94, 92)], fill=(120, 45, 38, 230))
        d.text((W - 80, 86), "N", font=font(20, bold=True), fill=(60, 45, 30, 255), anchor="mm")
        d.text((44, H - 62), "Mapa starego traktu", font=font(24, italic=True), fill=(68, 52, 35, 255))
    elif kind == "tea":
        for y in range(H):
            v = 28 + int(y * 22 / H)
            d.line([(0, y), (W, y)], fill=(v, 14 + v // 2, 10 + v // 3, 255))
        d.ellipse([110, 210, 500, 420], fill=(35, 22, 16, 255), outline=(181, 129, 61, 255), width=4)
        d.ellipse([150, 242, 460, 388], fill=(137, 76, 26, 210))
        d.ellipse([182, 258, 428, 370], fill=(179, 112, 44, 220))
        for x in range(0, W, 16):
            d.line([(x, 0), (x - 60, H)], fill=(255, 225, 165, 12), width=3)
        d.text((54, 48), "HERBATA", font=font(38, bold=True), fill=(240, 200, 130, 255))
        d.text((58, 98), "liść, woda, czas", font=font(22, italic=True), fill=(220, 175, 110, 255))
    elif kind == "stars":
        img.paste((8, 14, 28, 255), [0, 0, W, H])
        for _ in range(220):
            x, y = rnd.randrange(W), rnd.randrange(H)
            a = rnd.randrange(100, 255)
            r = rnd.choice([1, 1, 1, 2])
            d.ellipse([x - r, y - r, x + r, y + r], fill=(220, 232, 250, a))
        pts = [(70, 310), (160, 160), (260, 250), (345, 75), (430, 205), (545, 120), (590, 280)]
        d.line(pts, fill=(150, 190, 240, 120), width=2)
        for x, y in pts:
            d.ellipse([x - 7, y - 7, x + 7, y + 7], outline=(220, 232, 255, 230), width=2)
        d.text((44, 32), "Mapa północnego nieba", font=font(25, bold=True), fill=(220, 232, 250, 255))
    elif kind == "city":
        img.paste((196, 202, 192, 255), [0, 0, W, H])
        d.rectangle([0, 300, W, H], fill=(63, 85, 94, 255))
        for i in range(13):
            x = 12 + i * 51
            h = rnd.randrange(80, 230)
            d.rectangle([x, 300 - h, x + 42, 300], fill=(rnd.randrange(38, 72), rnd.randrange(48, 82), rnd.randrange(55, 88), 255))
            for yy in range(300 - h + 12, 288, 20):
                for xx in range(x + 8, x + 38, 13):
                    if rnd.random() > 0.45:
                        d.rectangle([xx, yy, xx + 5, yy + 7], fill=(230, 188, 110, 190))
        d.arc([-60, 120, 210, 390], 200, 340, fill=(230, 214, 180, 255), width=6)
        d.text((44, 32), "Nadmiejskie nabrzeże", font=font(25, bold=True), fill=(45, 50, 53, 255))
    elif kind == "recipe":
        img.paste((250, 242, 220, 255), [0, 0, W, H])
        d.ellipse([70, 55, 430, 415], fill=(235, 196, 120, 255), outline=(150, 109, 54, 255), width=4)
        d.ellipse([116, 100, 384, 370], fill=(248, 225, 168, 255))
        for _ in range(28):
            x = rnd.randrange(150, 350)
            y = rnd.randrange(125, 340)
            col = rnd.choice([(84, 120, 54, 230), (160, 56, 38, 230), (206, 172, 86, 230)])
            d.ellipse([x - 5, y - 3, x + 5, y + 3], fill=col)
        d.text((470, 170), "PRZEPIS", font=font(30, bold=True), fill=(115, 76, 40, 255))
        d.text((470, 210), "domowy", font=font(24, italic=True), fill=(115, 76, 40, 255))
    elif kind == "moon":
        img.paste((17, 27, 51, 255), [0, 0, W, H])
        d.ellipse([190, 55, 450, 315], fill=(233, 226, 194, 255))
        d.ellipse([245, 40, 505, 300], fill=(17, 27, 51, 255))
        for _ in range(120):
            x, y = rnd.randrange(W), rnd.randrange(H)
            d.ellipse([x, y, x + 1, y + 1], fill=(220, 230, 255, rnd.randrange(80, 220)))
        d.text((44, 32), "Nocna mapa", font=font(25, bold=True), fill=(225, 230, 240, 255))
    else:
        img.paste((214, 203, 177, 255), [0, 0, W, H])
        for _ in range(3000):
            x, y = rnd.randrange(W), rnd.randrange(H)
            v = rnd.randrange(-10, 11)
            r, g, b, a = img.getpixel((x, y))
            d.point((x, y), fill=(max(0, r + v), max(0, g + v), max(0, b + v), a))
        d.text((44, 32), kind.upper(), font=font(25, bold=True), fill=(90, 74, 50, 255))

    if fmt in ("png", "gif", "bmp"):
        # Keep the alpha channel for the transparent PNG test.
        if fmt == "png":
            img.save(path, "PNG")
        elif fmt == "gif":
            img.convert("RGB").save(path, "GIF")
        else:
            img.convert("RGB").save(path, "BMP")
    else:
        img.convert("RGB").save(path, "JPEG", quality=86)


def compass_png(path):
    W = H = 480
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    cx = cy = W // 2
    for rr in (205, 168, 130):
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=(90, 68, 42, 230), width=3)
    for ang in range(0, 360, 15):
        x1 = cx + int(205 * math.cos(math.radians(ang)))
        y1 = cy + int(205 * math.sin(math.radians(ang)))
        x2 = cx + int((215 if ang % 45 == 0 else 210) * math.cos(math.radians(ang)))
        y2 = cy + int((215 if ang % 45 == 0 else 210) * math.sin(math.radians(ang)))
        d.line([x1, y1, x2, y2], fill=(90, 68, 42, 220), width=2)
    d.polygon([(cx, cy - 150), (cx - 22, cy), (cx, cy + 150), (cx + 22, cy)],
              fill=(126, 48, 38, 225), outline=(61, 40, 24, 255))
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(235, 197, 122, 255))
    d.text((cx - 8, cy - 205), "N", font=font(30, bold=True), fill=(61, 40, 24, 255))
    img.save(path, "PNG")


def xhtml_bytes(body, title="Rozdział", css="../styles/book.css"):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <meta charset="utf-8"/>
  <title>{escape(title)}</title>
  <link rel="stylesheet" type="text/css" href="{css}"/>
</head>
<body>
{body}
</body>
</html>
'''.encode("utf-8")


def build_epub(path, meta, chapters, css, cover_path, images, nav_title=None, toc_extra=None):
    """chapters: list of (id, title, body_html). images: list of (name, bytes, media).
    toc_extra: {chapter_id: [(title, fragment), ...]} adds nested TOC entries."""
    toc_extra = toc_extra or {}
    title = meta["title"]
    author = meta["author"]
    lang = meta.get("lang", "pl")
    nav_title = nav_title or title
    with ZipFile(path, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
        z.writestr("META-INF/container.xml", '''<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
''', compress_type=ZIP_DEFLATED)
        z.writestr("OEBPS/styles/book.css", css, compress_type=ZIP_DEFLATED)
        manifest = []
        spine = []
        for idx, (cid, ctitle, body) in enumerate(chapters):
            z.writestr(f"OEBPS/text/{cid}.xhtml",
                       xhtml_bytes(body, ctitle, "../styles/book.css"),
                       compress_type=ZIP_DEFLATED)
            manifest.append(f'<item id="{cid}" href="text/{cid}.xhtml" media-type="application/xhtml+xml"/>')
            spine.append(f'<itemref idref="{cid}"/>')
        z.writestr("OEBPS/text/cover.xhtml", xhtml_bytes(
            '<div class="cover"><img src="../images/cover.jpg" alt="Okładka"/></div>',
            "Okładka", "../styles/book.css"), compress_type=ZIP_DEFLATED)
        z.writestr("OEBPS/images/cover.jpg", Path(cover_path).read_bytes(), compress_type=ZIP_DEFLATED)
        for name, data, media in images:
            z.writestr(f"OEBPS/images/{name}", data, compress_type=ZIP_DEFLATED)
        # EPUB3 navigation document
        nav_parts = []
        for cid, ctitle, _ in chapters:
            subs = toc_extra.get(cid) or []
            if subs:
                sub = "\n".join(
                    f'            <li><a href="{cid}.xhtml#{escape(frag)}">{escape(st)}</a></li>'
                    for st, frag in subs)
                nav_parts.append(f'      <li><a href="{cid}.xhtml">{escape(ctitle)}</a>\n        <ol>\n{sub}\n        </ol>\n      </li>')
            else:
                nav_parts.append(f'      <li><a href="{cid}.xhtml">{escape(ctitle)}</a></li>')
        nav_items = "\n".join(nav_parts)
        z.writestr("OEBPS/text/nav.xhtml", f'''<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>{escape(nav_title)}</title></head>
<body>
  <nav epub:type="toc" id="toc">
    <h1>Spis treści</h1>
    <ol>
{nav_items}
    </ol>
  </nav>
</body>
</html>
''', compress_type=ZIP_DEFLATED)
        # EPUB2 NCX
        point_parts = []
        np_id = 0
        for idx, (cid, ctitle, _) in enumerate(chapters):
            np_id += 1
            subs = toc_extra.get(cid) or []
            inner = ""
            for st, frag in subs:
                np_id += 1
                inner += f'''        <navPoint id="np{np_id}" playOrder="{np_id}">
          <navLabel><text>{escape(st)}</text></navLabel>
          <content src="text/{cid}.xhtml#{escape(frag)}"/>
        </navPoint>\n'''
            point_parts.append(f'''    <navPoint id="np{idx+1}" playOrder="{idx+1}">
      <navLabel><text>{escape(ctitle)}</text></navLabel>
      <content src="text/{cid}.xhtml"/>
{inner}    </navPoint>''')
        points = "\n".join(point_parts)
        z.writestr("OEBPS/toc.ncx", f'''<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="{escape(meta['uid'])}"/></head>
  <docTitle><text>{escape(title)}</text></docTitle>
  <navMap>
{points}
  </navMap>
</ncx>
''', compress_type=ZIP_DEFLATED)
        image_manifest = "\n".join(
            f'    <item id="img{i}" href="images/{name}" media-type="{media}"/>'
            for i, (name, _, media) in enumerate(images))
        opf = f'''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">{escape(meta['uid'])}</dc:identifier>
    <dc:title>{escape(title)}</dc:title>
    <dc:creator>{escape(author)}</dc:creator>
    <dc:language>{lang}</dc:language>
    <meta property="dcterms:modified">2026-10-26T09:00:00Z</meta>
    <meta name="cover" content="coverimg"/>
  </metadata>
  <manifest>
    <item id="nav" href="text/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="styles/book.css" media-type="text/css"/>
    <item id="coverpage" href="text/cover.xhtml" media-type="application/xhtml+xml"/>
    <item id="coverimg" href="images/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
{image_manifest}
{chr(10).join('    ' + m for m in manifest)}
  </manifest>
  <spine toc="ncx">
    <itemref idref="coverpage" linear="yes"/>
{chr(10).join('    ' + s for s in spine)}
  </spine>
  <guide>
    <reference type="cover" title="Okładka" href="text/cover.xhtml"/>
    <reference type="toc" title="Spis treści" href="text/nav.xhtml"/>
  </guide>
</package>
'''
        z.writestr("OEBPS/content.opf", opf, compress_type=ZIP_DEFLATED)


BASE_CSS = '''@charset "utf-8";
body { font-family: serif; line-height: 1.30; margin: 0; }
p { text-indent: 1.15em; margin: 0 0 0.35em 0; text-align: justify; }
p.first, p.noindent { text-indent: 0; }
.lead { font-style: italic; text-indent: 0; color: #4b3a27; }
h1 { font-size: 1.55em; text-align: center; margin: 0.55em 0 0.7em; page-break-before: always; }
h2 { font-size: 1.16em; margin: 0.8em 0 0.35em; }
h3 { font-size: 1em; font-weight: bold; margin: 0.6em 0 0.25em; }
blockquote { margin: 0.65em 1.1em; padding-left: 0.7em; border-left: 2px solid #a58b63; font-style: italic; color: #5a4633; }
ul, ol { margin: 0.35em 0 0.55em 1.2em; padding-left: 1em; }
li { margin: 0.12em 0; text-align: left; }
.center { text-align: center; text-indent: 0; }
.right { text-align: right; text-indent: 0; }
.small { font-size: 0.82em; }
.tiny { font-size: 0.7em; }
.big { font-size: 1.3em; }
.italic { font-style: italic; }
.bold { font-weight: bold; }
.mono { font-family: monospace; }
.half { width: 55%; }
@media screen { .note { font-style: italic; color: #6b3d2a; border-left: 1px solid #b49c76; padding-left: 0.8em; } }
.dropcap { font-size: 2em; font-weight: bold; font-style: normal; }
.ornament { text-align: center; text-indent: 0; margin: 0.65em 0; color: #8a6c43; }
hr { border: 0; border-top: 1px solid #b49c76; margin: 0.9em 15%; }
figure { text-align: center; margin: 0.7em 0; page-break-inside: avoid; }
figcaption { font-size: 0.78em; font-style: italic; color: #66513a; text-align: center; text-indent: 0; }
img { max-width: 100%; }
table { border-collapse: collapse; margin: 0.55em auto; font-size: 0.84em; width: 92%; }
th, td { border: 1px solid #9e8765; padding: 0.16em 0.3em; text-align: left; }
th { background-color: #d9c8a8; font-weight: bold; }
pre { font-family: monospace; font-size: 0.78em; margin: 0.5em 0.8em; white-space: pre-wrap; }
a { color: #4b607a; text-decoration: none; }
sup { font-size: 0.65em; }
.cover { text-align: center; margin: 0; }
.cover img { width: 100%; }
.poem { text-align: center; text-indent: 0; margin: 0.15em 0; }
.poem-title { text-align: center; text-indent: 0; font-style: italic; margin-bottom: 0.6em; }
.toc-note { margin-top: 1em; text-align: center; font-style: italic; text-indent: 0; }
'''


def para(text, cls=""):
    return f'<p{" class=\"" + cls + "\"" if cls else ""}>{text}</p>'


def chapter_body(title, paras, extra=""):
    out = [f"<h1>{escape(title)}</h1>"]
    for i, p in enumerate(paras):
        out.append(para(p, "first" if i == 0 else ""))
    out.append(extra)
    return "\n".join(out)


def main():
    LIB.mkdir(exist_ok=True)
    work = ROOT / "library_src"
    work.mkdir(exist_ok=True)
    covers = work / "covers"
    imgs = work / "images"
    covers.mkdir(exist_ok=True)
    imgs.mkdir(exist_ok=True)

    specs = [
        ("Sekret Starej Biblioteki", "Alicja Nowak", (36, 60, 48), (12, 28, 24), (207, 176, 106), "arch", "Powieść"),
        ("Podróż do Wnętrza Ziemi", "Juliusz Verne", (86, 32, 29), (28, 16, 17), (220, 177, 108), "orbit", "Klasyka"),
        ("Krótka Historia Herbaty", "Maria Kowalska", (51, 71, 40), (21, 35, 19), (213, 173, 91), "leaf", "Esej"),
        ("Wiersze z Ogrodu", "Jan Lis", (67, 53, 89), (27, 22, 43), (214, 184, 126), "leaf", "Poezja"),
        ("Atlas Nieba", "Piotr Zieliński", (24, 38, 68), (8, 14, 28), (186, 199, 228), "orbit", "Album"),
        ("Kuchnia Domowa", "Anna B.", (103, 58, 35), (41, 22, 16), (235, 194, 130), "arch", "Przepisy"),
        ("Cień nad Morskim Miastem", "Marek Wisła", (32, 56, 65), (14, 25, 31), (173, 198, 199), "wave", "Kryminał"),
        ("Mały Podróżnik", "Zofia Bajor", (91, 51, 90), (38, 23, 48), (235, 197, 126), "tower", "Dla dzieci"),
        ("Notatki o Architekturze", "Tomasz Ryk", (58, 54, 49), (25, 24, 22), (205, 187, 154), "arch", "Eseje"),
        ("Zaginiony Rękopis", "Ewa Kawa", (74, 55, 33), (28, 20, 12), (222, 188, 120), "tower", "Powieść"),
        ("Pustynny Wiatr", "Rafał Piasek", (120, 76, 38), (47, 30, 17), (231, 196, 138), "wave", "Opowieść"),
        ("Ostatni Latarnik", "Nina Morska", (33, 48, 52), (13, 23, 26), (196, 207, 190), "wave", "Nowela"),
    ]
    cover_files = {}
    for title, author, c1, c2, ac, motif, sub in specs:
        cp = covers / (str(abs(hash(title)) % 100000) + ".jpg")
        cover(cp, title, author, c1, c2, ac, motif, sub)
        cover_files[title] = cp

    # ---------------------------------------------------------------- book 1
    t1 = "Sekret Starej Biblioteki"
    img_map = imgs / "map.png"
    illustration(img_map, "map", "png")
    img_city = imgs / "city.jpg"
    illustration(img_city, "city", "jpg")
    img_diagram = imgs / "diagram.bmp"
    illustration(img_diagram, "diagram", "bmp")
    img_stars = imgs / "stars.png"
    illustration(img_stars, "stars", "png")
    img_compass = imgs / "compass.png"
    compass_png(img_compass)
    img_city_gif = imgs / "city.gif"
    illustration(img_city_gif, "city", "gif")

    b1_ch1 = chapter_body("Dom przy ulicy Cichej", [
        "Deszcz stukał o parapet od wczesnego ranka, a mimo to pani Róża nie odsunęła zasłony. Siedziała w fotelu, trzymając na kolanach niewielką księgę oprawioną w ciemną skórę, i nasłuchiwała. W starym domu każdy dźwięk miał swoje znaczenie: skrzypienie schodów, szelest papieru, ciche westchnienie zegara.",
        "Kiedy w końcu wstała, w pokoju unosił się zapach kurzu i suszonych ziół. Na biurku leżał list bez adresu. Koperta była zamknięta lakiem w kolorze wina, a na nim odciśnięty został znak, którego nikt w mieście nie potrafił rozpoznać.",
        "— To niemożliwe — szepnęła, odsuwając krzesło. — Przecież ta biblioteka spłonęła dawno temu.",
    ], '''
    <p class="ornament">• &nbsp; • &nbsp; •</p>
    <p class="first">Za oknem przemykał cień. Róża poprawiła okulary i po raz pierwszy od wielu lat poczuła, że coś ją woła. Nie był to strach, lecz ciekawość — najstarsza i najniebezpieczniejsza z ludzkich pokus.</p>
    ''')

    b1_ch2 = chapter_body("Klucz z mosiądzu", [
        "Biblioteka miejska stała na rogu placu, tuż obok kamiennej fontanny. Jej drzwi były zamknięte od tygodni, ale na tyłach budynku zawsze paliło się jedno okno. Róża znała to miejsce lepiej niż własny dom — spędziła tu trzydzieści lat, porządkując katalogi, które nikt już nie czytał.",
        "Tego wieczoru znalazła klucz w kieszeni płaszcza. Był ciężki i ciepły, jakby ktoś trzymał go w dłoni przez długi czas. Na jego trzonie wyryto słowa, które mogły być modlitwą albo adresem.",
        "— Nie powinnam tam wchodzić — powiedziała do siebie. — Ale przecież nikt inny nie wie, co jest w piwnicy.",
    ], '''
    <figure>
      <img src="../images/map.png" alt="Stara mapa traktu" width="560" height="315"/>
      <figcaption>Fragment planu z archiwum miejskiego. Czarny punkt oznacza wejście do podziemi.</figcaption>
    </figure>
    <blockquote>
      „Kto czyta, ten nigdy nie jest sam” — głosił napis nad wejściem do czytelni. Róża powtarzała go jak zaklęcie, ilekroć ogarniała ją samotność.
    </blockquote>
    <h2 id="katalog">Rzeczy znalezione w katalogu</h2>
    <ul>
      <li>trzy karty czytelnika z 1928 roku,</li>
      <li>zasuszony liść dębu,</li>
      <li>fotografia dziewczynki z książką,</li>
      <li>klucz, który nie pasował do żadnych drzwi.</li>
    </ul>
    <figure><img class="half" src="../images/compass.png" width="500" height="500" alt="Kompas"/>
    <figcaption>Kompas z szuflady Róży — przezroczyste tło.</figcaption></figure>
    <p style="color:#7a2e2e; font-style:italic; text-indent:0;">Notatka na marginesie: ten sam symbol pojawia się na okładce.</p>
    <p class="note">Przypis redaktora: rysunek pochodzi z prywatnego archiwum.</p>
    ''')

    b1_ch3 = chapter_body("Podziemia", [
        "Piwnica pachniała wilgotnym kamieniem i starym atramentem. Róża szła wzdłuż regałów, których nikt nie ruszał od dziesięcioleci. Niektóre półki były puste, inne uginały się pod ciężarem tomów. Wszystkie miały na grzbietach ten sam znak: otwarte oko wpisane w okrąg.",
        "W końcu dotarła do drzwi ukrytych za zasłoną z aksamitu. Otworzyły się bez oporu. Za nimi nie było jednak ciemności, lecz długi korytarz oświetlony mdłym blaskiem lamp olejnych.",
        "Na końcu korytarza znajdował się stół, a na stole — księga. Jej okładka była ciepła, jakby dopiero co leżała w słońcu.",
    ], '''
    <h3 id="instrukcja">Instrukcja obsługi księgi</h3>
    <ol>
      <li>Usiąść wygodnie i wyciszyć telefon.</li>
      <li>Otworzyć księgę na stronie oznaczonej wstążką.</li>
      <li>Czytać powoli, nie pomijając przypisów.</li>
      <li>Nie próbować czytać w pośpiechu — księga tego nie lubi.</li>
    </ol>
    <figure>
      <img src="../images/city.jpg" alt="Nadbrzeże po zmroku" width="560" height="315"/>
      <figcaption>Nadbrzeże po zmroku — według rysunku z opowieści Róży.</figcaption>
    </figure>
    <pre>ROZDZIAŁ I     str. 1
ROZDZIAŁ II    str. 17
ROZDZIAŁ III   str. 42</pre>
    ''')

    b1_ch4 = chapter_body("Nota na marginesie", [
        "Róża przeczytała pierwszą stronę dwa razy. Potem trzeci raz, powoli, wodząc palcem po linijkach. Księga nie opowiadała historii — przypominała ją. Każde zdanie odnosiło się do czegoś, co wydarzyło się w mieście dawno temu, i do czegoś, co dopiero miało się wydarzyć.",
        "Najbardziej zdziwił ją przypis na dole strony: maleńki, napisany ręką, którą znała. To było jej własne pismo. Nie pamiętała jednak, żeby kiedykolwiek tu była.",
    ], '''
    <table>
      <tr><th>Data</th><th>Wydarzenie</th><th>Świadek</th></tr>
      <tr><td>12 III</td><td>Zamknięcie biblioteki</td><td>R. Nowak</td></tr>
      <tr><td>18 III</td><td>Odnalezienie klucza</td><td>R. Nowak</td></tr>
      <tr><td>21 III</td><td>Pierwsze czytanie</td><td>—</td></tr>
      <tr><td>22 III</td><td>Powrót do podziemi</td><td>?</td></tr>
    </table>
    <figure><img src="../images/diagram.bmp" width="320" height="180" alt="Schemat"/>
    <figcaption>Schemat katalogu — plik BMP.</figcaption></figure>
    <figure><img src="../images/city.gif" width="320" height="180" alt="Nadbrzeże"/>
    <figcaption>Nadbrzeże — pierwsza klatka pliku GIF.</figcaption></figure>
    <p class="first">Za drzwiami rozległ się dźwięk dzwonka. Róża uniosła głowę. Na górze ktoś czekał.</p>
    <p class="center">KONIEC ROZDZIAŁU</p>
    ''')

    css1 = BASE_CSS + '''
h1 { color: #2e4b3a; }
.dropcap { color: #6a4a25; }
.lead { font-size: 1.05em; }
p.first::first-line { font-variant: small-caps; }
'''
    b1_images = [
        ("map.png", img_map.read_bytes(), "image/png"),
        ("city.jpg", img_city.read_bytes(), "image/jpeg"),
        ("diagram.bmp", img_diagram.read_bytes(), "image/bmp"),
        ("stars.png", img_stars.read_bytes(), "image/png"),
        ("compass.png", img_compass.read_bytes(), "image/png"),
        ("city.gif", img_city_gif.read_bytes(), "image/gif"),
    ]
    b1_chapters = [
        ("ch1", "Dom przy ulicy Cichej", b1_ch1),
        ("ch2", "Klucz z mosiądzu", b1_ch2),
        ("ch3", "Podziemia", b1_ch3),
        ("ch4", "Nota na marginesie", b1_ch4),
    ]
    build_epub(LIB / "01-Sekret-Starej-Biblioteki.epub",
               {"title": t1, "author": "Alicja Nowak", "uid": "urn:uuid:secret-library-0001"},
               b1_chapters, css1, cover_files[t1], b1_images,
               toc_extra={"ch2": [("Rzeczy znalezione w katalogu", "katalog")],
                          "ch3": [("Instrukcja obsługi księgi", "instrukcja")]})

    # ---------------------------------------------------------------- book 2
    t2 = "Podróż do Wnętrza Ziemi"
    img_moon = imgs / "moon.png"
    illustration(img_moon, "moon", "png")
    img_rock = imgs / "rock.jpg"
    illustration(img_rock, "rock", "jpg")
    b2_ch = []
    for n, (title, lead, body) in enumerate([
        ("Stary pergamin", "Wszystko zaczęło się od kartki papieru, którą mój stryj znalazł w książce kupionej na targu staroci.", "Pergamin był pożółkły i poplamiony, a litery zdawały się tańczyć, kiedy próbowało się je odczytać. Stryj twierdził, że to szyfr. Ja widziałem w nim tylko opowieść o podróży, której nikt nie odbył."),
        ("Zejście", "Dziura w ziemi wyglądała jak zwykły lej po meteorycie. Dopiero jej wnętrze okazało się bramą.", "Kiedy opuściliśmy się niżej, powietrze zrobiło się ciężkie i ciepłe. Światło latarek odbijało się od ścian pokrytych błyszczącymi żyłami. Nigdzie nie było śladu ludzi — tylko kamień, cisza i nieustanny szum wody."),
        ("Morze pod ziemią", "Nie wierzyłem własnym oczom. Przed nami rozciągało się jezioro większe niż wszystkie, które znałem.", "Fale uderzały o brzeg z dźwiękiem przypominającym odległy grzmot. Woda była czarna jak atrament, a nad nią unosiła się mgła. Stryj rozłożył mapę i przez długą chwilę milczał."),
        ("Powrót", "Nie wiem, ile dni minęło. Czas pod ziemią płynie inaczej.", "W końcu zobaczyliśmy światło. Było blade i chłodne, ale nigdy nie wydało mi się piękniejsze. Wyszliśmy na powierzchnię w miejscu, którego nie potrafiliśmy odnaleźć na żadnej mapie."),
    ]):
        extra = ""
        if n == 1:
            extra = '''<figure><img src="../images/moon.png" alt="Nocna mapa" width="560" height="315"/><figcaption>Mapa sporządzona przy świetle księżyca.</figcaption></figure>'''
        if n == 2:
            extra = '''<p class="center italic">— Czy to koniec? — zapytałem.<br/>— To dopiero początek — odpowiedział stryj.</p>
            <figure><img src="../images/rock.jpg" alt="Ściana jaskini" width="560" height="315"/><figcaption>Ściana jaskini pokryta żyłami minerałów.</figcaption></figure>'''
        b2_ch.append((f"r{n+1}", title, chapter_body(title, [lead] + [body] * 2, extra)))
    build_epub(LIB / "02-Podroz-do-Wnetrza-Ziemi.epub",
               {"title": t2, "author": "Juliusz Verne", "uid": "urn:uuid:journey-0002"},
               b2_ch, BASE_CSS + "h1 { color: #6b2d28; }\n", cover_files[t2],
               [("moon.png", img_moon.read_bytes(), "image/png"),
                ("rock.jpg", img_rock.read_bytes(), "image/jpeg")])

    # ---------------------------------------------------------------- book 3
    t3 = "Krótka Historia Herbaty"
    img_tea = imgs / "tea.jpg"
    illustration(img_tea, "tea", "jpg")
    b3 = chapter_body("Liść, woda, czas", [
        "Herbata nie jest zwykłym naparem. Przez wieki była lekarstwem, walutą i powodem do wojen. Wystarczy garść liści i odrobina cierpliwości, by przenieść się do innego świata.",
        "Najstarsze źródła opisują picie herbaty w górach południowych Chin. Początkowo traktowano ją jako zioło, później stała się codziennym rytuałem. Wraz z podróżnikami dotarła do Japonii, Persji i Europy.",
        "Dobra herbata wymaga dwóch rzeczy: świeżej wody i uwagi. Reszta — filiżanka, czajnik, cisza — jest dodatkiem, który czyni ten zwyczaj przyjemniejszym.",
    ], '''
    <blockquote>„Każda filiżanka jest małą podróżą. Ważne tylko, dokąd chcemy dojechać.” — stary poradnik herbaciany</blockquote>
    <h2>Rodzaje herbaty</h2>
    <table>
      <tr><th>Rodzaj</th><th>Utlenienie</th><th>Temperatura</th></tr>
      <tr><td>Biała</td><td>minimalne</td><td>75–80 °C</td></tr>
      <tr><td>Zielona</td><td>niskie</td><td>70–80 °C</td></tr>
      <tr><td>Oolong</td><td>średnie</td><td>85–90 °C</td></tr>
      <tr><td>Czarna</td><td>pełne</td><td>90–95 °C</td></tr>
      <tr><td>Pu-erh</td><td>fermentacja</td><td>95–100 °C</td></tr>
    </table>
    <h2>Rytuał w pięciu krokach</h2>
    <ol>
      <li>Zagrzej wodę i pozwól jej chwilę odpocząć.</li>
      <li>Wsyp liście do ogrzanego naczynia.</li>
      <li>Zalej je wodą i odczekaj tyle, ile wymaga herbata.</li>
      <li>Przelej napar i poczekaj, aż nieco ostygnie.</li>
      <li>Pij powoli. Naprawdę powoli.</li>
    </ol>
    <figure><img src="../images/tea.jpg" alt="Filiżanka herbaty" width="560" height="315"/>
    <figcaption>Światło późnego popołudnia na filiżance z czarnej porcelany.</figcaption></figure>
    <p class="small right">Na podstawie notatek autorki.</p>
    ''')
    b3b = chapter_body("Ogród herbaty", [
        "Ogród herbaciany wygląda jak morze zieleni. Krzewy rosną tarasami, a między nimi pracują zbieracze. Najlepsze liście zbiera się wczesnym rankiem, gdy na roślinach wciąż osadza się wilgoć.",
        "Wiele zależy od gleby, wysokości i cienia. Ta sama odmiana może dawać zupełnie inny smak zaledwie kilka kilometrów dalej.",
    ], "<p class=\"center italic\">Ciąg dalszy w następnym zeszycie.</p>")
    build_epub(LIB / "03-Krotka-Historia-Herbaty.epub",
               {"title": t3, "author": "Maria Kowalska", "uid": "urn:uuid:tea-0003"},
               [("h1", "Liść, woda, czas", b3), ("h2", "Ogród herbaty", b3b)],
               BASE_CSS + "h1 { color: #32502e; } table { font-size: 0.8em; }\n",
               cover_files[t3], [("tea.jpg", img_tea.read_bytes(), "image/jpeg")])

    # ---------------------------------------------------------------- book 4
    t4 = "Wiersze z Ogrodu"
    poems = [
        ("Poranek", ["Na trawie kropla światła,", "mała jak ziarno maku.", "Ogród się budzi cicho,", "jeszcze bez pośpiechu."]),
        ("Jabłoń", ["Stara jabłoń pamięta", "każde dziecko, które", "kradło jej jabłka", "i wracało po latach."]),
        ("Deszcz", ["Pisze po szybie", "cudzym pismem.", "Nie próbuj czytać —", "po prostu słuchaj."]),
        ("Wieczór", ["Cień kładzie się na ścieżce", "jak zmęczony pies.", "Zamykam furtkę", "i zostawiam dzień za sobą."]),
    ]
    poem_html = []
    for i, (pt, verses) in enumerate(poems):
        poem_html.append(f'<h2 class="poem-title">{escape(pt)}</h2>')
        for v in verses:
            poem_html.append(f'<p class="poem">{escape(v)}</p>')
        if i != len(poems) - 1:
            poem_html.append('<p class="ornament">•</p>')
    b4 = "\n".join(poem_html)
    build_epub(LIB / "04-Wiersze-z-Ogrodu.epub",
               {"title": t4, "author": "Jan Lis", "uid": "urn:uuid:poems-0004"},
               [("w1", "Wiersze zebrane", b4)],
               BASE_CSS + "h1 { color: #4d3967; }\n.poem { font-size: 1.08em; margin: 0.08em 0; }\n",
               cover_files[t4], [])

    # ---------------------------------------------------------------- book 5
    t5 = "Atlas Nieba"
    img_stars2 = imgs / "stars2.jpg"
    illustration(img_stars2, "stars", "jpg")
    img_moon2 = imgs / "moon2.png"
    illustration(img_moon2, "moon", "png")
    b5 = chapter_body("Mapa, która nie istnieje", [
        "Niebo zmienia się każdej nocy. Gwiazdy, które widzimy zimą, latem ustępują miejsca innym konstelacjom. Atlas nie jest więc mapą stałą — jest zaproszeniem do patrzenia.",
        "Najprostsze obserwacje są najlepsze. Wystarczy ciemne miejsce, chwila cierpliwości i chęć, by zobaczyć coś, co wydaje się odległe o tysiące lat.",
    ], '''
    <figure><img src="../images/stars2.jpg" alt="Gwiazdozbiory" width="570" height="320"/>
    <figcaption>Letni trójkąt i droga mleczna — schemat orientacyjny.</figcaption></figure>
    <h2>Trzy punkty odniesienia</h2>
    <ul><li>Wega — jasna gwiazda w Lutni,</li><li>Deneb — ogon Łabędzia,</li><li>Altair — najjaśniejsza w Orle.</li></ul>
    <p class="center">•</p>
    <figure><img src="../images/moon2.png" alt="Księżyc" width="570" height="320"/>
    <figcaption>Fazy Księżyca widziane z półkuli północnej (schemat).</figcaption></figure>
    <blockquote>Najpiękniejsze niebo jest tam, gdzie nikt nie zapalił latarni.</blockquote>
    ''')
    build_epub(LIB / "05-Atlas-Nieba.epub",
               {"title": t5, "author": "Piotr Zieliński", "uid": "urn:uuid:atlas-0005"},
               [("a1", "Mapa, która nie istnieje", b5)],
               BASE_CSS + "h1 { color: #223557; } body { line-height: 1.35; }\n",
               cover_files[t5], [("stars2.jpg", img_stars2.read_bytes(), "image/jpeg"),
                                 ("moon2.png", img_moon2.read_bytes(), "image/png")])

    # ---------------------------------------------------------------- book 6
    t6 = "Kuchnia Domowa"
    img_recipe = imgs / "recipe.jpg"
    illustration(img_recipe, "recipe", "jpg")
    b6 = chapter_body("Chleb na zakwasie", [
        "Dobry chleb zaczyna się na długo przed pieczeniem. Zakwas potrzebuje czasu, ciepła i odrobiny uwagi. Nie da się go przyspieszyć, ale można nauczyć się z nim żyć.",
        "Najważniejsza jest woda. Jeśli jest twarda, chleb rośnie wolniej. Jeśli zbyt miękka, ciasto traci strukturę. Doświadczenie przychodzi z kolejnymi bochenkami.",
    ], '''
    <figure><img src="../images/recipe.jpg" alt="Składniki na chleb" width="400" height="400"/>
    <figcaption>Mąka, woda, sól i cierpliwość.</figcaption></figure>
    <h2>Składniki</h2>
    <ul><li>500 g mąki pszennej,</li><li>350 ml wody,</li><li>100 g zakwasu,</li><li>10 g soli.</li></ul>
    <table>
      <tr><th>Etap</th><th>Czas</th></tr>
      <tr><td>Autoliza</td><td>30 min</td></tr>
      <tr><td>Fermentacja</td><td>3–4 h</td></tr>
      <tr><td>Pieczenie</td><td>45 min</td></tr>
    </table>
    <p class="small italic">Przepis z zeszytu babci, przepisany bez zmian.</p>
    ''')
    build_epub(LIB / "06-Kuchnia-Domowa.epub",
               {"title": t6, "author": "Anna B.", "uid": "urn:uuid:cook-0006"},
               [("c1", "Chleb na zakwasie", b6)],
               BASE_CSS + "h1 { color: #6b3c24; }\n", cover_files[t6],
               [("recipe.jpg", img_recipe.read_bytes(), "image/jpeg")])

    # ---------------------------------------------------------------- books 7-12: simpler
    authored = [
        ("Cień nad Morskim Miastem", "Marek Wisła", 7, ["Mgła", "List", "Port", "Odpływ"],
         (40, 64, 73), "h1 { color: #24404a; }"),
        ("Mały Podróżnik", "Zofia Bajor", 8, ["Pociąg", "Las", "Jezioro", "Powrót"],
         (102, 59, 96), "body { font-size: 1.1em; line-height: 1.4; } h1 { color: #5b3060; }"),
        ("Notatki o Architekturze", "Tomasz Ryk", 9, ["Kamień", "Światło", "Miasto", "Pamięć"],
         (67, 62, 55), "h1 { color: #4e4941; }"),
        ("Zaginiony Rękopis", "Ewa Kawa", 10, ["Noc", "Biblioteka", "Ogień", "Odnalezienie"],
         (92, 67, 42), "h1 { color: #5f4328; }"),
        ("Pustynny Wiatr", "Rafał Piasek", 11, ["Karawana", "Studnia", "Burza", "Oaza"],
         (139, 87, 44), "h1 { color: #7a4d21; }"),
        ("Ostatni Latarnik", "Nina Morska", 12, ["Latarnia", "Statek", "Sztorm", "Świt"],
         (45, 61, 65), "h1 { color: #2b4045; }"),
    ]
    for title, author, num, chap_titles, hue, extra_css in authored:
        chs = []
        for ci, ct in enumerate(chap_titles):
            body = chapter_body(ct, [
                f"To opowieść numer {num}, rozdział {ci + 1}. Światło padało na kartki, a każda z nich zdawała się kryć własną tajemnicę.",
                "Bohaterowie tej historii mieli niewiele czasu, ale wystarczająco dużo odwagi, by zrobić pierwszy krok. Reszta przyszła sama — jak zwykle, gdy człowiek przestaje czekać.",
                "Za oknem krajobraz zmieniał się powoli. Zaczynał się nowy rozdział, choć nikt nie wiedział jeszcze, jak się skończy.",
            ], f'<p class="center">— {ci + 1} —</p>')
            chs.append((f"r{ci+1}", ct, body))
        build_epub(LIB / f"{num:02d}-{title.replace(' ', '-')}.epub",
                   {"title": title, "author": author, "uid": f"urn:uuid:book-{num:04d}"},
                   chs, BASE_CSS + extra_css + "\n", cover_files[title], [])

    print(f"Generated {len(list(LIB.glob('*.epub')))} EPUB files in {LIB}")
    for p in sorted(LIB.glob("*.epub")):
        print(f"  {p.name:40s} {p.stat().st_size/1024:8.1f} KiB")


if __name__ == "__main__":
    main()

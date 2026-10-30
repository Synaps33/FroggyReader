#!/usr/bin/env python3
"""Create tiny edge-case EPUB fixtures (empty, corrupt, missing cover/TOC)."""
import sys
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED, ZIP_DEFLATED

root = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eg_edge")
(root / "empty").mkdir(parents=True, exist_ok=True)
(root / "corrupt").mkdir(parents=True, exist_ok=True)
(root / "nocover").mkdir(parents=True, exist_ok=True)
(root / "corrupt" / "Uszkodzona.epub").write_bytes(b"not a real epub" * 128)
with ZipFile(root / "nocover" / "Bez-Okladki.epub", "w") as z:
    z.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
    z.writestr("META-INF/container.xml", '''<?xml version="1.0"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>''', compress_type=ZIP_DEFLATED)
    z.writestr("OEBPS/content.opf", '''<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Bez Okładki</dc:title><dc:creator>Test</dc:creator>
    <dc:language>pl</dc:language><dc:identifier>urn:uuid:nocover</dc:identifier>
  </metadata>
  <manifest><item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/></manifest>
  <spine><itemref idref="c1"/></spine>
</package>''', compress_type=ZIP_DEFLATED)
    z.writestr("OEBPS/ch1.xhtml", '''<?xml version="1.0"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Bez okładki</title>
<style>body{margin:0}p{text-align:justify;text-indent:1em}h1{text-align:center}</style></head>
<body><h1>Bez okładki</h1>
<p>To jest książka testowa bez okładki i bez pliku NCX.</p>
<p>Powinna pojawić się okładka zastępcza oraz syntetyczny spis treści.</p>
</body></html>''', compress_type=ZIP_DEFLATED)
print(root)

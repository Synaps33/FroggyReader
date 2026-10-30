# FroggyReader — test report

Date: 2026-10-26. Tests were run on an x86_64 host, on MIPS32 soft-float code
under `qemu-mipsel`, and as final SF2000/GB300 core builds.

## Environment

- Host: GCC 14, 320x240 software framebuffer, runner without SDL
- MIPS: `mipsel-linux-muslsf` + `qemu-mipsel`, `-msoft-float -static`
- Device: `mipsel-mti-elf`, `sf2000_multicore`, bundles in `dist/`
- SD card used for installation: GB300 FAT32 card

## Build and hard checks

| Check | Result |
|---|---|
| Hardware-FPU instructions in `epub_libretro_sf2000.a` | 0 |
| Hardware-FPU instructions in the final SF2000 core | 0 |
| Hardware-FPU instructions in the final GB300 core | 0 |
| Final SF2000 ELF: text / data / bss | 4,392,752 / 3,224 / 3,352 B |
| Device bundles | `dist/epub.sf2k`, `dist/epub.gb3k` (about 4.2 MB each) |
| Final GB300 installation | `system/Deimos/cores/epub.{sf2k,gb3k}`, `cores/epub/core_87000000` |

Note: the final console firmware also contains its own soft-float routines from
newlib (for example for `video_sf2000`), but there are **no FPU instructions**.
The reader itself performs no floating-point calculations; text layout and image
scaling are integer-only.

## Host functional tests

`make test`: **exit 0**, 101 PNG captures, no exceptions, segfaults, or parsing
errors. Every scenario is reproducible from a clean state.

| Area | What was checked | Result |
|---|---|---|
| Scanning | 12 EPUBs, progress, clean screen, no footer under the bar | OK |
| 3x2 library | 62x82 covers, titles under covers, flat Kindle-style UI | OK |
| Marquee | smooth scrolling of a selected long title | OK |
| UI languages | English (default), Polish, German, Japanese; selection saved | OK |
| Japanese menus | Japanese glyphs in menus, TOC, settings, and help | OK |
| Empty folder | message and instructions | OK (`edge/empty_001.png`) |
| Corrupt file | skipped without a crash | OK (`edge/corrupt_001.png`) |
| Missing cover/NCX | fallback cover, synthetic TOC | OK (`edge/nocover_*`) |
| Book cover | separate page, undistorted scaling | OK |
| Text | justification, indentation, headings, italics, bold, super/subscript | OK |
| Page breaking | paragraph splitting, no widows/single-line pages in tests | OK |
| Smooth scrolling | Up/Down in 4 px steps, page entry/exit, chapter transition | OK |
| Tables | headers, borders, cell wrapping, splitting across pages | OK |
| Lists | ordered and unordered, hanging indent | OK |
| Quotes/frames | background, left border for the full quote height | OK |
| Images | JPEG, PNG, alpha PNG, GIF (first frame), BMP | OK |
| CSS images | `width`/`max-width`, aspect ratio preserved | OK |
| TOC | EPUB3 nav, nested entries, NCX, jump to `#anchor` | OK |
| Settings | theme, text size 15 to 19, justification, margins, reflow | OK |
| Themes | Paper, Sepia, Night; dark CSS colors adapted in Night | OK |
| Bookmarks | add, list, jump to page, delete | OK |
| Persistence | close and reopen: chapter 3, page 4/5 | OK |
| TOC fallback | synthetic "Chapter N" entries | OK |

## MIPS / QEMU tests

`make test-mips`: **exit 0**, 42 PNG captures, the same layout and graphics as
the host build. Covered: library scanning, cover, text, TOC, images, page
transitions, smooth scrolling, title marquee, and the Japanese interface.
Captures: `screenshots/mips/`.

Example QEMU user-mode times (indicative, not a hardware benchmark):

- 12 books and 20 frames: about 0.6 s
- 55 frames including opening a book, TOC, and images: about 0.7 s

Real timing on an 87 MHz MIPS CPU will differ; QEMU does not model SD-card
latency or cache behavior accurately. A short hands-on test on hardware is
still recommended.

## Key screenshots

- `screenshots/final/lib_001.png` — library scan
- `screenshots/final/lib_013.png` — 3x2 Kindle-style library with selection
- `screenshots/final/marquee_064.png` — scrolling long book title
- `screenshots/final/shelf_015.png` — second shelf (12 books)
- `screenshots/final/read_016.png` — book cover page
- `screenshots/final/read_021.png` — text page with justification and indents
- `screenshots/final/images_045.png` — JPEG image page with caption
- `screenshots/final/scroll_046.png` — smooth scrolling in the middle of a page
- `screenshots/final/scroll_190.png` — scrolling after a chapter change
- `screenshots/final/table_046.png` — table with a header
- `screenshots/final/toc_028.png` — nested table of contents
- `screenshots/final/night_031.png` — Night theme in the reader
- `screenshots/final/read_072.png` — Night theme in the library
- `screenshots/final/reflow_043.png` — larger text and wider margins
- `screenshots/final/bookmarks_035.png` — bookmarks
- `screenshots/final/info_037.png` — book information
- `screenshots/final/help_017.png` — help screen
- `screenshots/final/lang_034.png` — Polish interface
- `screenshots/final/lang_058.png` — German interface
- `screenshots/final/lang_082.png` — Japanese interface
- `screenshots/final/langread_016.png` — Japanese help
- `screenshots/final/langread_032.png` — Japanese book menu
- `screenshots/final/resume_015.png` — resume after restart
- `screenshots/edge/nocover_001.png` — fallback cover
- `screenshots/edge/nocover_019.png` — synthetic TOC
- `screenshots/mips/images_045.png` — the same image on MIPS

## Bugs found and fixed during testing

1. Corrupted word endings after multi-byte UTF-8 characters (buffer not zeroed
   after `realloc`).
2. Lost last word of a paragraph and single-element headings.
3. Screen not refreshed after D-pad navigation in the library.
4. Selected book on the second shelf compared against a local grid index.
5. Quote border drawn only at the height of the first line.
6. Over-justification of short lines (limited to 4 px per gap).
7. Incorrect percentage interpretation in the `margin` shorthand.
8. Heading bold style overwritten by inheritance from the parent.
9. CSS `width` ignored when an image also had a `height` attribute.
10. White/red UI accents caused by 24-bit colors used where the framebuffer is
    RGB565.
11. Added smooth Up/Down scrolling with full-page caching; Left/Right still
    turns pages as before.
12. Changed the library grid from 4x2 to 3x2 and enlarged the covers.
13. Replaced the dark-gold shelf with a clean, flat Kindle-style interface:
    DejaVu Sans in the UI, white/gray background, blue accent, titles under
    covers.
14. Added four UI languages (English by default) with Japanese glyphs; removed
    the footer under the library loading bar.
15. Added smooth, back-and-forth marquee scrolling for a selected long title
    under its cover. The animation redraws only the title strip, not the whole
    library.

## Limitations

The full list is in the README. The main ones: no SVG, DRM, RTL, or complex
script shaping; a CSS subset without `float`/`position`/`columns`; no
dictionary hyphenation, search, or metadata cache. The Japanese interface is
fully translated and has UI glyphs, but full Japanese book text needs a larger
CJK font. The test did not include a physical console: the build, installation,
and MIPS behavior under QEMU were verified.

#!/usr/bin/env bash
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")" && pwd)"
DIST="$PROJECT/dist"
SD="${1:-}"
INSTALL_SAMPLES=1
for arg in "$@"; do
    case "$arg" in
        --no-samples) INSTALL_SAMPLES=0 ;;
    esac
done
if [ "${1:-}" = "--no-samples" ]; then SD="${2:-}"; fi

if [ -z "$SD" ]; then
    for p in /media/Sajnaps/GB300 /media/Sajnaps/SF2000 /media/mmcblk* /media/* /mnt/*; do
        if [ -d "$p/system/Deimos/cores" ] || [ -d "$p/cores" ]; then SD="$p"; break; fi
    done
fi
if [ -z "$SD" ] || [ ! -d "$SD" ]; then
    echo "No SD card detected. Usage: $0 [--no-samples] <path_to_card>"
    exit 1
fi

IS_GB300=0
if [ "${FORCE_GB300:-0}" = "1" ] || [[ "$SD" =~ GB300 ]] || [ -f "$SD/system/Deimos/cores/gpsp.gb3k" ]; then
    IS_GB300=1
fi
if [ "$IS_GB300" -eq 1 ]; then
    CORE="$DIST/epub.gb3k"
    echo "Detected GB300 / GB300V2"
else
    CORE="$DIST/epub.sf2k"
    echo "Detected Data Frog SF2000"
fi
if [ ! -f "$CORE" ]; then
    echo "Missing $CORE - run: make device"
    exit 1
fi

echo "Installing FroggyReader to: $SD"

mkdir -p "$SD/system/Deimos/cores" "$SD/cores/epub" "$SD/ROMS/epub" \
         "$SD/ROMS/epub/books" "$SD/ROMS/epub/.res" "$SD/ROMS/.res" \
         "$SD/configs/custom_cores/epub" "$SD/system/logs"
[ -d "$SD/ROMS/gba" ] || mkdir -p "$SD/ROMS/gba"

# The loader always opens <core>.sf2k; install the device-appropriate build
# under both names (same convention as the other custom cores on this card).
cp -f "$CORE" "$SD/system/Deimos/cores/epub.sf2k"
cp -f "$CORE" "$SD/system/Deimos/cores/epub.gb3k"
cp -f "$CORE" "$SD/cores/epub/core_87000000"
cp -f "$CORE" "$SD/ROMS/epub/epub.sf2k"
cp -f "$CORE" "$SD/ROMS/epub/epub.gb3k"

# Multicore stubs.
printf 'epub;epub;EPUB.gba' > "$SD/ROMS/EPUB.gba"
printf 'epub;epub;EPUB.gba' > "$SD/ROMS/epub/EPUB.gba"
printf 'epub;epub;EPUB.gba' > "$SD/ROMS/gba/EPUB.gba"
cat > "$SD/configs/custom_cores/epub/games.opt" <<'GAMES'
EPUB.gba|epub
EPUB|epub
GAMES

# Menu artwork.
if [ -f "$DIST/res/EPUB.rgb565" ]; then
    for d in "$SD/ROMS" "$SD/ROMS/epub" "$SD/ROMS/epub/.res" "$SD/ROMS/.res" "$SD/ROMS/gba"; do
        cp -f "$DIST/res/EPUB.rgb565" "$d/EPUB.rgb565"
    done
    cp -f "$DIST/res/EPUB.png" "$SD/ROMS/epub/EPUB.png"
fi

# Small, generated public-domain test library.
if [ "$INSTALL_SAMPLES" -eq 1 ] && [ -d "$PROJECT/library" ]; then
    cp -f "$PROJECT"/library/*.epub "$SD/ROMS/epub/books/"
    cat > "$SD/ROMS/epub/books/README.txt" <<'README'
Sample books generated for reader testing.
You can delete them and copy your own .epub files into this folder.
README
fi

: > "$SD/ROMS/epub/epub.log"
: > "$SD/system/logs/epub.log"
sync
echo "=========================================================="
echo " FroggyReader installed."
echo " Launch from menu: ROMS/EPUB.gba"
echo " Core:            system/Deimos/cores/epub.sf2k"
echo " Books:           ROMS/epub/books/"
echo " Settings:        system/saves/epub_reader/reader.cfg"
echo " Log:             ROMS/epub/epub.log"
echo "=========================================================="

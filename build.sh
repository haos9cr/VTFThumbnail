#!/usr/bin/env bash
# Cross-compiles VTFThumbnail.dll (x64) with MinGW. On Windows use VTFThumbnail.sln instead.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$ROOT/build}"
mkdir -p "$OUT"
x86_64-w64-mingw32-windres -I "$ROOT/src" "$ROOT/src/VTFThumbnail.rc" -O coff -o "$OUT/res.o"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -municode -DUNICODE -D_UNICODE -DNOMINMAX \
    -D_WIN32_WINNT=0x0601 -I "$ROOT/src" -shared \
    "$ROOT"/src/Dll.cpp "$ROOT"/src/ThumbnailProvider.cpp "$ROOT"/src/Thumbnail.cpp \
    "$ROOT"/src/vtf/VtfReader.cpp "$OUT/res.o" "$ROOT/src/VTFThumbnail.def" \
    -o "$OUT/VTFThumbnail.dll" -s -static -static-libgcc -static-libstdc++ \
    -lole32 -luuid -lshell32 -ladvapi32 -lgdi32
echo "built $OUT/VTFThumbnail.dll"

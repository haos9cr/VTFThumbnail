#!/usr/bin/env bash
# Runs the test suite on Linux.
#
#   tests/run_tests.sh          core: thumbnail code vs srctools + Pillow, sanitizer fuzzing
#   tests/run_tests.sh --wine   + the real DLL (MinGW build) driven like Explorer under Wine,
#                                 and regsvr32 registration / unregistration
#
# Requirements: g++, python3, `pip install srctools pillow`;
# for --wine also x86_64-w64-mingw32-g++ / -windres and wine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
WINE_TESTS=0
[[ "${1:-}" == "--wine" ]] && WINE_TESTS=1
rm -rf "$OUT"
mkdir -p "$OUT"
cd "$OUT"

CXX="${CXX:-g++}"
CORE="$ROOT/src/Thumbnail.cpp $ROOT/src/vtf/VtfReader.cpp"
PY="python3 -I"
fail=0
step() { echo; echo "=== $* ==="; }
run() { if ! "$@"; then echo "*** FAILED: $*"; fail=1; fi; }
run_tail() {
    local n=$1; shift
    if "$@" > step.log 2>&1; then tail -n "$n" step.log; else cat step.log; echo "*** FAILED: $*"; fail=1; fi
}

step "Thumbnail core vs srctools + Pillow (mip choice, scaling, formats, alpha, broken files)"
$CXX -std=c++17 -O2 -Wall -Wextra -I "$ROOT/src" "$ROOT/tests/thumb_dump.cpp" $CORE -o thumb_dump
run_tail 3 $PY "$ROOT/tests/validate.py" core ./thumb_dump

step "Fuzzing with AddressSanitizer + UBSan"
$CXX -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I "$ROOT/src" \
    "$ROOT/tests/fuzz.cpp" $CORE -o fuzz
run ./fuzz "${FUZZ_ITERATIONS:-30000}" core/fmt_DXT5.vtf core/fmt_BGRA8888.vtf core/ver_2.vtf core/ver_3.vtf \
    core/cube_4.vtf core/anim3.vtf core/npot_nomip.vtf core/wide_512x128.vtf core/fmt_I8.vtf

if [[ $WINE_TESTS == 1 ]]; then
    export WINEDEBUG=-all WINEPREFIX="$OUT/wineprefix"
    step "Cross-compiling VTFThumbnail.dll and fake_explorer.exe"
    "$ROOT/build.sh" "$OUT/dll" | tail -1
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -DUNICODE -D_UNICODE "$ROOT/tests/fake_explorer.cpp" \
        -o fake_explorer.exe -static -lole32 -luuid -lshlwapi -lgdi32 -ladvapi32
    DLL="$OUT/dll/VTFThumbnail.dll"

    step "DLL under Wine, driven like Explorer (same checks as the core)"
    run_tail 3 $PY "$ROOT/tests/validate.py" wine "$ROOT/tests/wine_thumb.sh" "$OUT/fake_explorer.exe" "$DLL"

    step "regsvr32: register, keep + restore another handler, per-user, unregister"
    run bash "$ROOT/tests/registration.sh" "$OUT/fake_explorer.exe" "$DLL" core/rgba_1024.vtf
fi

echo
if [[ $fail == 0 ]]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $fail

#!/usr/bin/env bash
# validate.py adapter: runs VTFThumbnail.dll through fake_explorer.exe under Wine.
#   wine_thumb.sh <fake_explorer.exe> <dll> <in.vtf> <maxSize> <alphaMode> <out.bgra>
# The alpha mode goes through the registry, like for the real DLL.
set -euo pipefail
exe=$1 dll=$2 in=$3 size=$4 mode=$5 out=$6
state="$WINEPREFIX/.alpha_mode"
if [[ "$(cat "$state" 2>/dev/null)" != "$mode" ]]; then
    wine reg add 'HKCU\Software\VTFThumbnail' /v AlphaMode /t REG_DWORD /d "$mode" /f > /dev/null
    echo "$mode" > "$state"
fi
w() { winepath -w "$1"; }
touch "$out"
wine "$exe" thumb "$(w "$dll")" "$(w "$in")" "$size" "$(w "$out")" | tr -d '\r'

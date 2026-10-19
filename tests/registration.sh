#!/usr/bin/env bash
# Registration checks under Wine (Wine runs as administrator, so plain regsvr32 = all users).
#   registration.sh <fake_explorer.exe> <VTFThumbnail.dll> <sample.vtf>
set -uo pipefail
exe=$1 dll="$(winepath -w "$2")" vtf="$(winepath -w "$3")" out="$(winepath -w "$PWD/reg.bgra")"
fail=0
CLSID='{1228785B-BB07-4147-A1D8-21F55D235915}'
OTHER='{11111111-2222-3333-4444-555555555555}'
HANDLER='Software\Classes\.vtf\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}'
dump() { wine "$exe" reg-dump | tr -d '\r'; }
expect() {  # expect <description> <grep pattern> [absent]
    if dump | grep -qF -- "$2"; then found=1; else found=0; fi
    if [[ "${3:-}" == absent ]]; then found=$((1 - found)); fi
    if [[ $found == 1 ]]; then echo "OK   $1"; else echo "FAIL $1"; dump; fail=1; fi
}
cocreate() {
    local r; r=$(wine "$exe" cocreate "$vtf" 64 "$out" | tr -d '\r')
    if [[ "$r" == "$1"* ]]; then echo "OK   CoCreateInstance: $r"; else echo "FAIL CoCreateInstance: $r (want $1)"; fail=1; fi
}

wine reg add "HKLM\\$HANDLER" /ve /d "$OTHER" /f > /dev/null
cocreate "ERROR cocreate 80040154"

wine regsvr32 /s "$dll" || { echo "FAIL regsvr32"; fail=1; }
expect "all users: InprocServer32 = DLL path" "InprocServer32 [] = $dll"
expect "all users: ThreadingModel Apartment" "[ThreadingModel] = Apartment"
expect "all users: .vtf thumbnail handler" "HKLM\\$HANDLER [] = $CLSID"
expect "all users: approved shell extension" "Approved [$CLSID] = VTF Thumbnail Provider"
cocreate "64 64 1"

wine regsvr32 /s /u "$dll" || { echo "FAIL regsvr32 /u"; fail=1; }
expect "unregister: previous handler restored" "HKLM\\$HANDLER [] = $OTHER"
expect "unregister: CLSID removed" "InprocServer32 [] = $dll" absent
expect "unregister: approval removed" "Approved [$CLSID] = VTF" absent

wine regsvr32 /s /n /i:user "$dll" || { echo "FAIL regsvr32 /i:user"; fail=1; }
expect "per user: HKCU handler" "HKCU\\$HANDLER [] = $CLSID"
expect "per user: HKLM untouched" "HKLM\\$HANDLER [] = $OTHER"

wine regsvr32 /s "$dll"
expect "all users after per-user: HKCU entry removed" "HKCU\\$HANDLER [] = $CLSID" absent
wine regsvr32 /s /u "$dll"
expect "final: nothing of ours left" "= $CLSID" absent
expect "final: no approval left" "Approved [$CLSID] = VTF" absent
wine reg delete "HKLM\\Software\\Classes\\.vtf" /f > /dev/null 2>&1
exit $fail

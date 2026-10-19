# make_release.ps1 - packs a built DLL into dist\VTFThumbnail-<version>.zip
#
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Configuration Release]
#
# Run after building Release | x64. The version comes from src\Version.h.
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root "bin\$Configuration\VTFThumbnail.dll"
if (-not (Test-Path -LiteralPath $dll)) { throw "Not found: $dll - build $Configuration | x64 first." }

# Privacy check: no local paths or Windows user name in the DLL (the project URL is allowed).
$bytes = [System.IO.File]::ReadAllBytes($dll)
$texts = @([System.Text.Encoding]::UTF8.GetString($bytes), [System.Text.Encoding]::Unicode.GetString($bytes))
$versionText = Get-Content -LiteralPath (Join-Path $root 'src\Version.h') -Raw
if ($versionText -match 'VTFTHUMB_URL\s+"([^"]+)"') {
    $url = [regex]::Escape($Matches[1])
    $texts = $texts | ForEach-Object { [regex]::Replace($_, $url, '', 'IgnoreCase') }
}
$needles = @($root, '\Users\', $env:USERNAME) | Where-Object { $_ -and $_.Length -ge 3 }
foreach ($needle in $needles) {
    foreach ($text in $texts) {
        if ($text.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "VTFThumbnail.dll contains '$needle' (a local path or your user name). Rebuild with the current VTFThumbnail.vcxproj (it uses /PDBALTPATH)."
        }
    }
}
Write-Host 'Privacy check: no local paths or user name in VTFThumbnail.dll'

$v = @('MAJOR', 'MINOR', 'PATCH') | ForEach-Object {
    if ($versionText -notmatch "VTFTHUMB_VERSION_$_\s+(\d+)") { throw 'Cannot read the version from src\Version.h' }
    $Matches[1]
}
$version = $v -join '.'

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist "VTFThumbnail-$version"
$zip = "$stage.zip"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $dll -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.cmd') -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall.cmd') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')
$readme = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release_readme.txt') -Raw -Encoding UTF8) -replace '\{version\}', $version
[System.IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, (New-Object System.Text.UTF8Encoding $true))

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip)
Remove-Item -LiteralPath $stage -Recurse -Force

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
Write-Host "Created $zip"
Write-Host "SHA-256: $hash"

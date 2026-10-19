@echo off
rem Registers VTFThumbnail.dll from this folder.
rem   Run as administrator -> thumbnails for all users.
rem   Normal double-click  -> only for you (no admin rights needed).
rem Keep the DLL in this folder afterwards: Explorer loads it from here.
setlocal
set "DLL=%~dp0VTFThumbnail.dll"
if not exist "%DLL%" (
    echo VTFThumbnail.dll not found next to this script.
    pause
    exit /b 1
)
"%SystemRoot%\System32\regsvr32.exe" "%DLL%"

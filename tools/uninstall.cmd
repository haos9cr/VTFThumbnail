@echo off
rem Removes the VTFThumbnail registration (current user, and all users if run as administrator).
rem Afterwards the DLL can be deleted (sign out or restart Explorer if Windows says it is in use).
setlocal
"%SystemRoot%\System32\regsvr32.exe" /u "%~dp0VTFThumbnail.dll"

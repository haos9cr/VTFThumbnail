// Module.h - DLL-wide state shared by the COM objects.
#pragma once

#include <windows.h>

// {1228785B-BB07-4147-A1D8-21F55D235915}
#define VTFTHUMB_CLSID_STRING L"{1228785B-BB07-4147-A1D8-21F55D235915}"
extern const CLSID CLSID_VTFThumbnailProvider;

extern HINSTANCE g_module;
void ModuleAddRef();
void ModuleRelease();

// Creates a VTFThumbnailProvider (ThumbnailProvider.cpp).
HRESULT CreateThumbnailProvider(REFIID riid, void** ppv);

// Registry settings: HKCU\Software\VTFThumbnail (then HKLM) value `name`.
DWORD ReadSetting(const wchar_t* name, DWORD fallback);

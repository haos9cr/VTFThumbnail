// fake_explorer.cpp - drives VTFThumbnail.dll the way Explorer does (runs under Wine).
//
//   fake_explorer.exe thumb <dll> in.vtf maxSize out.bgra   [direct DllGetClassObject]
//   fake_explorer.exe cocreate in.vtf maxSize out.bgra      [via the registry, after regsvr32]
//   fake_explorer.exe register <dll> | register-user <dll> | unregister <dll>
//   fake_explorer.exe reg-dump                               [prints the keys we care about]
// Thumb modes print "w h alpha" ("alpha" = 1 for WTSAT_ARGB) or "ERROR hr".
#include <windows.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

static const CLSID CLSID_Thumb = {0x1228785b, 0xbb07, 0x4147, {0xa1, 0xd8, 0x21, 0xf5, 0x5d, 0x23, 0x59, 0x15}};

static int Dump(IThumbnailProvider* tp, const wchar_t* in, UINT cx, const wchar_t* out) {
    IStream* stream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(in, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (FAILED(hr)) { std::printf("ERROR open %08lx\n", hr); return 1; }
    IInitializeWithStream* init = nullptr;
    hr = tp->QueryInterface(__uuidof(IInitializeWithStream), reinterpret_cast<void**>(&init));
    if (FAILED(hr)) { std::printf("ERROR qi %08lx\n", hr); return 1; }
    hr = init->Initialize(stream, STGM_READ);
    if (FAILED(hr)) { std::printf("ERROR init %08lx\n", hr); return 1; }
    if (init->Initialize(stream, STGM_READ) != HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED)) {
        std::printf("ERROR double init accepted\n"); return 1;
    }
    init->Release();
    stream->Release();  // the provider keeps its own reference

    HBITMAP bmp = nullptr;
    WTS_ALPHATYPE at = WTSAT_UNKNOWN;
    hr = tp->GetThumbnail(cx, &bmp, &at);
    if (FAILED(hr)) { std::printf("ERROR %08lx\n", hr); return 1; }
    DIBSECTION ds = {};
    // (Windows reports a top-down DIB with a negative biHeight; Wine reports it positive,
    // so orientation is checked by validate.py comparing pixels.)
    if (GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits) {
        std::printf("ERROR not a 32bpp DIB section (size %d, bpp %d)\n", int(sizeof(ds)), ds.dsBm.bmBitsPixel);
        return 1;
    }
    const int w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight;
    FILE* f = _wfopen(out, L"wb");
    std::fwrite(ds.dsBm.bmBits, 1, size_t(w) * h * 4, f);
    std::fclose(f);
    DeleteObject(bmp);
    std::printf("%d %d %d\n", w, h, at == WTSAT_ARGB ? 1 : 0);
    return 0;
}

static void DumpKey(HKEY root, const wchar_t* rootName, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    LSTATUS st = RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size);
    std::printf("%ls\\%ls [%ls] = %ls\n", rootName, key, value ? value : L"", st == ERROR_SUCCESS ? buf : L"<none>");
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const wchar_t* mode = argv[1];
    int rc = 2;

    if (!wcscmp(mode, L"thumb") && argc >= 6) {
        HMODULE dll = LoadLibraryW(argv[2]);
        if (!dll) { std::printf("ERROR load\n"); return 1; }
        auto get = reinterpret_cast<HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**)>(
            GetProcAddress(dll, "DllGetClassObject"));
        auto canUnload = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllCanUnloadNow"));
        IClassFactory* cf = nullptr;
        if (FAILED(get(CLSID_Thumb, IID_IClassFactory, reinterpret_cast<void**>(&cf)))) { std::printf("ERROR cf\n"); return 1; }
        IThumbnailProvider* tp = nullptr;
        if (FAILED(cf->CreateInstance(nullptr, __uuidof(IThumbnailProvider), reinterpret_cast<void**>(&tp)))) {
            std::printf("ERROR create\n"); return 1;
        }
        cf->Release();
        rc = Dump(tp, argv[3], UINT(_wtoi(argv[4])), argv[5]);
        tp->Release();
        if (canUnload() != S_OK) { std::printf("ERROR objects leaked (DllCanUnloadNow != S_OK)\n"); rc = 1; }
    } else if (!wcscmp(mode, L"cocreate") && argc >= 5) {
        IThumbnailProvider* tp = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_Thumb, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IThumbnailProvider),
                                      reinterpret_cast<void**>(&tp));
        if (FAILED(hr)) { std::printf("ERROR cocreate %08lx\n", hr); return 1; }
        rc = Dump(tp, argv[2], UINT(_wtoi(argv[3])), argv[4]);
        tp->Release();
    } else if ((!wcscmp(mode, L"register") || !wcscmp(mode, L"register-user") || !wcscmp(mode, L"unregister")) &&
               argc >= 3) {
        HMODULE dll = LoadLibraryW(argv[2]);
        if (!dll) { std::printf("ERROR load\n"); return 1; }
        HRESULT hr;
        if (!wcscmp(mode, L"register"))
            hr = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllRegisterServer"))();
        else if (!wcscmp(mode, L"register-user"))
            hr = reinterpret_cast<HRESULT(STDAPICALLTYPE*)(BOOL, PCWSTR)>(GetProcAddress(dll, "DllInstall"))(TRUE, L"user");
        else
            hr = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllUnregisterServer"))();
        std::printf("%ls -> %08lx\n", mode, hr);
        rc = FAILED(hr) ? 1 : 0;
    } else if (!wcscmp(mode, L"reg-dump")) {
        const wchar_t* clsid = L"Software\\Classes\\CLSID\\{1228785B-BB07-4147-A1D8-21F55D235915}\\InprocServer32";
        const wchar_t* handler = L"Software\\Classes\\.vtf\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
        for (int i = 0; i < 2; ++i) {
            HKEY root = i ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            const wchar_t* name = i ? L"HKCU" : L"HKLM";
            DumpKey(root, name, clsid, nullptr);
            DumpKey(root, name, clsid, L"ThreadingModel");
            DumpKey(root, name, handler, nullptr);
        }
        DumpKey(HKEY_LOCAL_MACHINE, L"HKLM", L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
                L"{1228785B-BB07-4147-A1D8-21F55D235915}");
        rc = 0;
    }
    CoUninitialize();
    return rc;
}

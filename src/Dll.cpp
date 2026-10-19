// Dll.cpp - COM server plumbing: class factory, DLL exports, registration.
//
// regsvr32 VTFThumbnail.dll            all users when run as administrator,
//                                      otherwise only the current user
// regsvr32 /n /i:user VTFThumbnail.dll current user only (no admin rights needed)
// regsvr32 /u VTFThumbnail.dll         removes both registrations
#include <windows.h>
#include <shlobj.h>
#include <thumbcache.h>

#include <new>
#include <string>

#include "Module.h"

const CLSID CLSID_VTFThumbnailProvider = {0x1228785b, 0xbb07, 0x4147, {0xa1, 0xd8, 0x21, 0xf5, 0x5d, 0x23, 0x59, 0x15}};

HINSTANCE g_module = nullptr;
static LONG g_moduleRefs = 0;

void ModuleAddRef() { InterlockedIncrement(&g_moduleRefs); }
void ModuleRelease() { InterlockedDecrement(&g_moduleRefs); }

DWORD ReadSetting(const wchar_t* name, DWORD fallback) {
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        DWORD value = 0, size = sizeof(value);
        if (RegGetValueW(root, L"Software\\VTFThumbnail", name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS)
            return value;
    }
    return fallback;
}

// --- class factory ---------------------------------------------------------------

namespace {

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { ModuleAddRef(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        return CreateThumbnailProvider(riid, ppv);
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) ModuleAddRef(); else ModuleRelease();
        return S_OK;
    }

private:
    ~ClassFactory() { ModuleRelease(); }
    LONG refs_ = 1;
};

} // namespace

// --- registration ----------------------------------------------------------------

namespace {

const wchar_t kName[] = L"VTF Thumbnail Provider";
const wchar_t kClsidKey[] = L"CLSID\\" VTFTHUMB_CLSID_STRING;
// {E357FCCD-A995-4576-B01F-234630154E96} = IThumbnailProvider handler slot.
const wchar_t kHandlerKey[] = L".vtf\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
const wchar_t kApprovedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
const wchar_t kBackupValue[] = L"PreviousThumbnailHandler";

LSTATUS SetString(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& value) {
    HKEY k;
    LSTATUS st = RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
    if (st != ERROR_SUCCESS) return st;
    st = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return st;
}

bool GetString(HKEY root, const wchar_t* key, const wchar_t* name, std::wstring& out) {
    wchar_t buf[512];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return false;
    out = buf;
    return true;
}

// Deletes `key` if it has neither subkeys nor values.
void DeleteIfEmpty(HKEY root, const wchar_t* key) {
    HKEY k;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    DWORD subkeys = 0, values = 0;
    const bool empty = RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
                                        nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS &&
                       subkeys == 0 && values == 0;
    RegCloseKey(k);
    if (empty) RegDeleteKeyW(root, key);
}

std::wstring ModulePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(g_module, &path[0], DWORD(path.size()));
        if (n == 0) return std::wstring();
        if (n < path.size()) { path.resize(n); return path; }
        path.resize(path.size() * 2);
    }
}

// `classes` = HKLM\Software\Classes or HKCU\Software\Classes.
LSTATUS Register(HKEY classes, bool machine) {
    const std::wstring path = ModulePath();
    if (path.empty()) return ERROR_FILE_NOT_FOUND;
    const std::wstring inproc = std::wstring(kClsidKey) + L"\\InprocServer32";

    LSTATUS st = SetString(classes, kClsidKey, nullptr, kName);
    if (st != ERROR_SUCCESS) return st;
    if ((st = SetString(classes, inproc.c_str(), nullptr, path)) != ERROR_SUCCESS) return st;
    if ((st = SetString(classes, inproc.c_str(), L"ThreadingModel", L"Apartment")) != ERROR_SUCCESS) return st;

    // Keep another program's handler so that unregistering puts it back.
    std::wstring previous;
    if (GetString(classes, kHandlerKey, nullptr, previous) && !previous.empty() &&
        _wcsicmp(previous.c_str(), VTFTHUMB_CLSID_STRING) != 0)
        SetString(classes, kClsidKey, kBackupValue, previous);
    if ((st = SetString(classes, kHandlerKey, nullptr, VTFTHUMB_CLSID_STRING)) != ERROR_SUCCESS) return st;

    if (machine) SetString(HKEY_LOCAL_MACHINE, kApprovedKey, VTFTHUMB_CLSID_STRING, kName);
    return ERROR_SUCCESS;
}

void Unregister(HKEY classes, bool machine) {
    std::wstring previous, current;
    GetString(classes, kClsidKey, kBackupValue, previous);
    if (GetString(classes, kHandlerKey, nullptr, current) && _wcsicmp(current.c_str(), VTFTHUMB_CLSID_STRING) == 0) {
        if (!previous.empty()) {
            SetString(classes, kHandlerKey, nullptr, previous);
        } else {
            RegDeleteKeyW(classes, kHandlerKey);
            DeleteIfEmpty(classes, L".vtf\\ShellEx");
            DeleteIfEmpty(classes, L".vtf");
        }
    }
    RegDeleteTreeW(classes, kClsidKey);
    RegDeleteKeyW(classes, kClsidKey);
    if (machine) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kApprovedKey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, VTFTHUMB_CLSID_STRING);
            RegCloseKey(k);
        }
    }
}

HKEY OpenClasses(bool machine) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, L"Software\\Classes", 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return nullptr;
    return k;
}

HRESULT RegisterScope(bool machine) {
    HKEY classes = OpenClasses(machine);
    if (!classes) return machine ? E_ACCESSDENIED : E_FAIL;
    const LSTATUS st = Register(classes, machine);
    RegCloseKey(classes);
    if (st != ERROR_SUCCESS) {
        if (HKEY c = OpenClasses(machine)) { Unregister(c, machine); RegCloseKey(c); }
        return HRESULT_FROM_WIN32(st);
    }
    if (machine) {  // a per-user registration would win over the new one (maybe an old path)
        if (HKEY user = OpenClasses(false)) { Unregister(user, false); RegCloseKey(user); }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

HRESULT UnregisterAll() {
    HRESULT hr = S_OK;
    for (bool machine : {false, true}) {
        if (HKEY classes = OpenClasses(machine)) {
            Unregister(classes, machine);
            RegCloseKey(classes);
        } else if (machine) {
            // An all-users registration we may not remove without administrator rights.
            const std::wstring key = std::wstring(L"Software\\Classes\\") + kClsidKey;
            HKEY k;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
                RegCloseKey(k);
                hr = E_ACCESSDENIED;
            }
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return hr;
}

} // namespace

// --- exports -----------------------------------------------------------------------

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_VTFThumbnailProvider) return CLASS_E_CLASSNOTAVAILABLE;
    ClassFactory* f = new (std::nothrow) ClassFactory();
    if (!f) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_moduleRefs == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    // All users if we may write HKLM, otherwise just this user.
    const HRESULT hr = RegisterScope(true);
    if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) return RegisterScope(false);
    return hr;
}

STDAPI DllUnregisterServer() { return UnregisterAll(); }

// regsvr32 /n /i:user  -> current user,  /n /i:machine -> all users,  /u /n /i -> remove.
STDAPI DllInstall(BOOL install, PCWSTR cmdLine) {
    if (!install) return UnregisterAll();
    if (cmdLine && _wcsicmp(cmdLine, L"user") == 0) return RegisterScope(false);
    if (cmdLine && _wcsicmp(cmdLine, L"machine") == 0) return RegisterScope(true);
    return DllRegisterServer();
}

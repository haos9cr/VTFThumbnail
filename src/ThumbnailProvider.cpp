// ThumbnailProvider.cpp - IThumbnailProvider for .vtf files.
//
// Explorer (or the COM surrogate it runs us in) creates the object, hands it the
// file as an IStream via IInitializeWithStream, then asks for a bitmap of at
// most cx x cx pixels.
#include <windows.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <cstring>
#include <new>

#include "Module.h"
#include "Thumbnail.h"

namespace {

class StreamSource : public thumb::Source {
public:
    explicit StreamSource(IStream* s) : stream_(s) {}

    uint64_t Size() override {
        STATSTG st = {};
        if (SUCCEEDED(stream_->Stat(&st, STATFLAG_NONAME))) return st.cbSize.QuadPart;
        LARGE_INTEGER zero = {};
        ULARGE_INTEGER end = {};
        if (SUCCEEDED(stream_->Seek(zero, STREAM_SEEK_END, &end))) return end.QuadPart;
        return 0;
    }

    bool ReadAt(uint64_t offset, void* buf, size_t n) override {
        LARGE_INTEGER pos;
        pos.QuadPart = LONGLONG(offset);
        if (FAILED(stream_->Seek(pos, STREAM_SEEK_SET, nullptr))) return false;
        uint8_t* p = static_cast<uint8_t*>(buf);
        while (n > 0) {
            const ULONG chunk = ULONG(n > (1u << 30) ? (1u << 30) : n);
            ULONG got = 0;
            const HRESULT hr = stream_->Read(p, chunk, &got);
            if (FAILED(hr) || got == 0) return false;
            p += got;
            n -= got;
        }
        return true;
    }

private:
    IStream* stream_;
};

class VTFThumbnailProvider final : public IThumbnailProvider, public IInitializeWithStream {
public:
    VTFThumbnailProvider() { ModuleAddRef(); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IThumbnailProvider))
            *ppv = static_cast<IThumbnailProvider*>(this);
        else if (riid == __uuidof(IInitializeWithStream))
            *ppv = static_cast<IInitializeWithStream*>(this);
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD /*grfMode*/) override {
        if (!stream) return E_INVALIDARG;
        if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream_ = stream;
        stream_->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;
        if (!stream_) return E_UNEXPECTED;
        try {
            thumb::Settings settings;
            settings.alphaMode = ReadSetting(L"AlphaMode", thumb::ALPHA_AUTO);

            StreamSource src(stream_);
            thumb::Result res;
            std::string error;
            if (!thumb::Make(src, cx, settings, res, error))
                return error == "out of memory" ? E_OUTOFMEMORY : E_FAIL;

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = LONG(res.width);
            bmi.bmiHeader.biHeight = -LONG(res.height);  // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bmp || !bits) {
                if (bmp) DeleteObject(bmp);
                return E_OUTOFMEMORY;
            }
            memcpy(bits, res.bgra.data(), res.bgra.size());
            *phbmp = bmp;
            *pdwAlpha = res.alpha ? WTSAT_ARGB : WTSAT_RGB;
            return S_OK;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_FAIL;
        }
    }

private:
    ~VTFThumbnailProvider() {
        if (stream_) stream_->Release();
        ModuleRelease();
    }

    LONG refs_ = 1;
    IStream* stream_ = nullptr;
};

} // namespace

HRESULT CreateThumbnailProvider(REFIID riid, void** ppv) {
    VTFThumbnailProvider* p = new (std::nothrow) VTFThumbnailProvider();
    if (!p) return E_OUTOFMEMORY;
    const HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}

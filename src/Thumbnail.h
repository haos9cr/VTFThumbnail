// Thumbnail.h - VTF -> thumbnail bitmap. Portable (no Windows headers), so it is
// tested on Linux; ThumbnailProvider.cpp wraps it for Explorer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vtf/VtfReader.h"

namespace thumb {

// Random access to the file. Explorer hands us an IStream; tests use a FILE*.
struct Source {
    virtual ~Source() = default;
    virtual uint64_t Size() = 0;
    // Reads exactly n bytes at `offset`; false on error / short read.
    virtual bool ReadAt(uint64_t offset, void* buf, size_t n) = 0;
};

enum AlphaMode : uint32_t {
    ALPHA_AUTO = 0,  // transparency, except for normal / SSBump maps (alpha is a mask there)
    ALPHA_ON   = 1,  // always show alpha as transparency
    ALPHA_OFF  = 2,  // always opaque
};

struct Settings {
    uint32_t alphaMode = ALPHA_AUTO;
};

struct Result {
    uint32_t width = 0, height = 0;   // <= the requested size, aspect ratio kept
    std::vector<uint8_t> bgra;        // top-down, B,G,R,A, straight (not premultiplied) alpha
    bool alpha = false;               // false: A is 255 everywhere
    uint32_t mip = 0;                 // mip level that was decoded
    vtf::ReadInfo info;
};

// Biggest image we decode (pixels). Textures larger than this without a
// suitable mip level get no thumbnail rather than a 1 GB allocation.
constexpr uint64_t MAX_DECODE_PIXELS = 8192ull * 8192ull;

// Builds a thumbnail that fits into maxSize x maxSize. Reads only the header
// and the one mip level it needs.
bool Make(Source& src, uint32_t maxSize, const Settings& settings, Result& out, std::string& error);

// Picks the smallest mip level that is still at least `maxSize` on its longer side
// (or level 0 if the texture is smaller than that).
uint32_t PickMip(const vtf::ReadInfo& info, uint32_t maxSize);

// Area-averaging downscale of an RGBA8 image (alpha-weighted, so transparent
// pixels don't bleed their colour). dw <= w, dh <= h.
void Downscale(const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh,
               std::vector<uint8_t>& out);

} // namespace thumb

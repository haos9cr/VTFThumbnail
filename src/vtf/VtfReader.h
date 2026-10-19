// VtfReader.h - dependency-free VTF reader (face / slice 0; whole file or one mip level).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "VtfFormat.h"

namespace vtf {

struct ReadInfo {
    uint32_t width = 0, height = 0;
    int      versionMinor = 0;
    int32_t  format = FMT_NONE;     // raw VTF image format id from the file
    uint32_t flags = 0;
    uint32_t frames = 1;            // animation frames
    uint32_t faces = 1;             // 6 or 7 for cubemaps
    uint32_t depth = 1;             // volume slices
    uint32_t mipCount = 1;
    bool     hasThumbnail = false;
    bool     hasAlpha = false;      // decoded image contains alpha != 255
};

// Name of any VTF image format id (incl. ones we can only read).
const char* AnyFormatName(int32_t format);

// Decodes the first frame/face/slice of the largest mip level into RGBA8.
bool Read(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba,
          ReadInfo& info, std::string& error);

// Decodes the largest mip level of every animation frame (first face / slice).
// info.hasAlpha is true if any frame has alpha.
bool ReadFrames(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& frames,
                ReadInfo& info, std::string& error);

// --- Partial reading (e.g. a thumbnail from one small mip) ---------------------

struct Layout {
    ReadInfo info;            // hasAlpha is not known yet (false)
    uint64_t imageData = 0;   // file offset of the image data (smallest mip first)
};

// Parses the header. `data` holds the first `avail` bytes of a file of `fileSize`
// bytes; it must cover the header incl. the resource list (see HeaderSize).
bool ParseHeader(const uint8_t* data, size_t avail, uint64_t fileSize, Layout& layout, std::string& error);

// Where one image (face 0, slice 0 of `frame` at mip level `mip`) is stored.
struct ImageRef {
    uint64_t offset = 0, size = 0;  // bytes in the file
    uint32_t width = 0, height = 0;
};
bool Locate(const Layout& layout, uint32_t mip, uint32_t frame, uint64_t fileSize, ImageRef& ref,
            std::string& error);

// Decodes `ref.size` bytes of `format` (as returned by Locate) into w*h RGBA8.
void DecodeImage(const uint8_t* src, int32_t format, uint32_t w, uint32_t h, uint8_t* rgba);

// Bytes ParseHeader needs, from the first 16 bytes of the file (0 if not a VTF).
inline uint32_t HeaderSize(const uint8_t* first16) {
    if (first16[0] != 'V' || first16[1] != 'T' || first16[2] != 'F' || first16[3] != 0) return 0;
    const uint32_t s = uint32_t(first16[12]) | (uint32_t(first16[13]) << 8) |
                       (uint32_t(first16[14]) << 16) | (uint32_t(first16[15]) << 24);
    return s < 80 ? 80 : s;
}

// True if the buffer starts with the VTF signature.
bool IsVtf(const uint8_t* data, size_t size);

} // namespace vtf

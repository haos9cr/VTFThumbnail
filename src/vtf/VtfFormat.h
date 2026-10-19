// VtfFormat.h - VTF constants (values match VTFLib / the Source SDK).
#pragma once

#include <cstdint>

namespace vtf {

constexpr int32_t FMT_NONE = -1;  // "no image" (e.g. no low-res thumbnail in the file)

// Texture flags used by the reader / thumbnailer.
enum : uint32_t {
    FLAG_NORMAL  = 0x00000080,
    FLAG_ENVMAP  = 0x00004000,  // cubemap
    FLAG_SSBUMP  = 0x08000000,
};

} // namespace vtf

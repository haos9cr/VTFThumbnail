// Thumbnail.cpp - VTF -> thumbnail bitmap (portable).
#include "Thumbnail.h"

#include <algorithm>
#include <new>

namespace thumb {

uint32_t PickMip(const vtf::ReadInfo& info, uint32_t maxSize) {
    for (uint32_t m = info.mipCount; m-- > 0;) {
        const uint32_t w = std::max(1u, info.width >> m), h = std::max(1u, info.height >> m);
        if (std::max(w, h) >= maxSize) return m;
    }
    return 0;
}

namespace {

struct Tap { uint32_t index; float weight; };

// For each output sample, the source samples it covers and their coverage.
// Weights of one output sample sum to 1.
void Taps(uint32_t src, uint32_t dst, std::vector<uint32_t>& start, std::vector<Tap>& taps) {
    start.assign(dst + 1, 0);
    taps.clear();
    const double scale = double(src) / dst;
    for (uint32_t i = 0; i < dst; ++i) {
        const double a = i * scale, b = (i + 1) * scale;
        start[i] = uint32_t(taps.size());
        for (uint32_t s = uint32_t(a); s < src && s < b; ++s) {
            const double cover = std::min<double>(b, s + 1) - std::max<double>(a, s);
            if (cover > 1e-9) taps.push_back({s, float(cover / scale)});
        }
    }
    start[dst] = uint32_t(taps.size());
}

} // namespace

void Downscale(const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh,
               std::vector<uint8_t>& out) {
    std::vector<uint32_t> xs, ys;
    std::vector<Tap> xt, yt;
    Taps(w, dw, xs, xt);
    Taps(h, dh, ys, yt);

    // Horizontal pass into premultiplied floats (r*a, g*a, b*a, a), a in 0..1.
    std::vector<float> tmp(size_t(dw) * h * 4, 0.0f);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = rgba + size_t(y) * w * 4;
        float* o = tmp.data() + size_t(y) * dw * 4;
        for (uint32_t x = 0; x < dw; ++x, o += 4) {
            for (uint32_t t = xs[x]; t < xs[x + 1]; ++t) {
                const uint8_t* p = row + size_t(xt[t].index) * 4;
                const float a = p[3] * (1.0f / 255.0f), wa = xt[t].weight * a;
                o[0] += p[0] * wa; o[1] += p[1] * wa; o[2] += p[2] * wa; o[3] += wa;
            }
        }
    }
    // Vertical pass, then back to straight alpha.
    out.assign(size_t(dw) * dh * 4, 0);
    for (uint32_t y = 0; y < dh; ++y) {
        for (uint32_t x = 0; x < dw; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (uint32_t t = ys[y]; t < ys[y + 1]; ++t) {
                const float* p = tmp.data() + (size_t(yt[t].index) * dw + x) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * yt[t].weight;
            }
            uint8_t* d = out.data() + (size_t(y) * dw + x) * 4;
            if (acc[3] > 1e-6f) {
                for (int c = 0; c < 3; ++c) d[c] = uint8_t(std::min(255.0f, acc[c] / acc[3] + 0.5f));
                d[3] = uint8_t(std::min(255.0f, acc[3] * 255.0f + 0.5f));
            }
        }
    }
}

bool Make(Source& src, uint32_t maxSize, const Settings& settings, Result& out, std::string& error) {
    try {
        out = Result();
        if (maxSize == 0) maxSize = 256;
        const uint64_t size = src.Size();

        // Header (incl. the 7.3+ resource list).
        uint8_t first[16];
        if (size < 64 || !src.ReadAt(0, first, sizeof(first))) { error = "not a VTF file"; return false; }
        const uint32_t headerSize = vtf::HeaderSize(first);
        if (headerSize == 0) { error = "not a VTF file"; return false; }
        if (headerSize > (1u << 20)) { error = "corrupt header"; return false; }
        std::vector<uint8_t> header(size_t(std::min<uint64_t>(size, std::max(headerSize, 4096u))));
        if (!src.ReadAt(0, header.data(), header.size())) { error = "read error"; return false; }

        vtf::Layout layout;
        if (!vtf::ParseHeader(header.data(), header.size(), size, layout, error)) return false;
        out.info = layout.info;

        // One mip level, frame 0, face 0, slice 0.
        out.mip = PickMip(layout.info, maxSize);
        vtf::ImageRef ref;
        if (!vtf::Locate(layout, out.mip, 0, size, ref, error)) return false;
        if (uint64_t(ref.width) * ref.height > MAX_DECODE_PIXELS) { error = "image too large"; return false; }
        std::vector<uint8_t> raw(size_t(ref.size));
        if (!src.ReadAt(ref.offset, raw.data(), raw.size())) { error = "read error"; return false; }
        std::vector<uint8_t> rgba(size_t(ref.width) * ref.height * 4, 255);
        vtf::DecodeImage(raw.data(), layout.info.format, ref.width, ref.height, rgba.data());
        raw = std::vector<uint8_t>();

        bool hasAlpha = false;
        for (size_t i = 3; i < rgba.size(); i += 4)
            if (rgba[i] != 255) { hasAlpha = true; break; }
        out.info.hasAlpha = hasAlpha;
        bool useAlpha = hasAlpha;
        if (settings.alphaMode == ALPHA_OFF) useAlpha = false;
        else if (settings.alphaMode == ALPHA_AUTO && (layout.info.flags & (vtf::FLAG_NORMAL | vtf::FLAG_SSBUMP)))
            useAlpha = false;
        if (hasAlpha && !useAlpha)
            for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
        out.alpha = useAlpha;

        // Fit into maxSize x maxSize, keeping the aspect ratio. Never upscale:
        // Explorer centres smaller images itself.
        uint32_t w = ref.width, h = ref.height;
        if (std::max(w, h) > maxSize) {
            uint32_t dw, dh;
            if (w >= h) { dw = maxSize; dh = std::max(1u, uint32_t((uint64_t(h) * maxSize + w / 2) / w)); }
            else        { dh = maxSize; dw = std::max(1u, uint32_t((uint64_t(w) * maxSize + h / 2) / h)); }
            std::vector<uint8_t> scaled;
            Downscale(rgba.data(), w, h, dw, dh, scaled);
            rgba.swap(scaled);
            w = dw; h = dh;
        }

        for (size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);  // RGBA -> BGRA
        out.width = w;
        out.height = h;
        out.bgra.swap(rgba);
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory";
        return false;
    }
}

} // namespace thumb

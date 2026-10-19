// fuzz.cpp - mutates sample VTFs and feeds them to the thumbnail core.
// Build with -fsanitize=address,undefined; any crash / sanitizer report is a failure.
// Usage: fuzz <iterations> sample1.vtf [sample2.vtf ...]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "Thumbnail.h"

struct MemSource : thumb::Source {
    const std::vector<uint8_t>& d;
    explicit MemSource(const std::vector<uint8_t>& data) : d(data) {}
    uint64_t Size() override { return d.size(); }
    bool ReadAt(uint64_t off, void* buf, size_t n) override {
        if (off > d.size() || n > d.size() - off) return false;
        std::memcpy(buf, d.data() + off, n);
        return true;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const long iterations = std::atol(argv[1]);
    std::vector<std::vector<uint8_t>> samples;
    for (int i = 2; i < argc; ++i) {
        FILE* f = std::fopen(argv[i], "rb");
        if (!f) return 2;
        std::vector<uint8_t> d;
        int c;
        while ((c = std::fgetc(f)) != EOF) d.push_back(uint8_t(c));
        std::fclose(f);
        samples.push_back(d);
    }
    std::mt19937 rng(12345);
    long ok = 0;
    const uint32_t sizes[] = {16, 32, 96, 256, 1024};
    for (long it = 0; it < iterations; ++it) {
        std::vector<uint8_t> d = samples[rng() % samples.size()];
        const int kind = rng() % 4;
        if (kind == 0) {          // flip random bytes, mostly in the header
            for (int n = 1 + rng() % 8; n-- > 0;) {
                const size_t lim = (rng() % 4) ? std::min<size_t>(d.size(), 128) : d.size();
                d[rng() % lim] = uint8_t(rng());
            }
        } else if (kind == 1) {   // truncate
            d.resize(rng() % (d.size() + 1));
        } else if (kind == 2) {   // interesting header values
            static const uint32_t vals[] = {0, 1, 3, 4, 0x7f, 0x80, 0xff, 0xffff, 0x10000, 0x7fffffff, 0xffffffff};
            const size_t pos = rng() % std::min<size_t>(d.size(), 120);
            const uint32_t v = vals[rng() % (sizeof(vals) / sizeof(vals[0]))];
            for (int b = 0; b < 4 && pos + b < d.size(); ++b) d[pos + b] = uint8_t(v >> (8 * b));
        } else {                  // both
            d[rng() % std::min<size_t>(d.size(), 80)] = uint8_t(rng());
            d.resize(rng() % (d.size() + 1));
        }
        MemSource src(d);
        thumb::Settings s;
        s.alphaMode = rng() % 3;
        thumb::Result r;
        std::string err;
        if (thumb::Make(src, sizes[rng() % 5], s, r, err)) {
            ++ok;
            if (r.bgra.size() != size_t(r.width) * r.height * 4) { std::printf("bad result size\n"); return 1; }
        }
    }
    std::printf("fuzz: %ld iterations, %ld decoded, no crashes\n", iterations, ok);
    return 0;
}

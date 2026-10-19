// thumb_dump.cpp - runs the thumbnail core (no COM) on a file and dumps the result.
// Usage: thumb_dump in.vtf maxSize alphaMode out.bgra
//   prints "w h alpha mip" or "ERROR <message>"
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Thumbnail.h"

struct FileSource : thumb::Source {
    FILE* f;
    explicit FileSource(FILE* file) : f(file) {}
    uint64_t Size() override {
        std::fseek(f, 0, SEEK_END);
        return uint64_t(std::ftell(f));
    }
    bool ReadAt(uint64_t off, void* buf, size_t n) override {
        return std::fseek(f, long(off), SEEK_SET) == 0 && std::fread(buf, 1, n, f) == n;
    }
};

int main(int argc, char** argv) {
    if (argc < 5) return 2;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    FileSource src(f);
    thumb::Settings s;
    s.alphaMode = uint32_t(std::atoi(argv[3]));
    thumb::Result r;
    std::string err;
    const bool ok = thumb::Make(src, uint32_t(std::atoi(argv[2])), s, r, err);
    std::fclose(f);
    if (!ok) { std::printf("ERROR %s\n", err.c_str()); return 1; }
    FILE* o = std::fopen(argv[4], "wb");
    std::fwrite(r.bgra.data(), 1, r.bgra.size(), o);
    std::fclose(o);
    std::printf("%u %u %d %u\n", r.width, r.height, int(r.alpha), r.mip);
    return 0;
}

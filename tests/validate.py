"""Builds test VTFs with srctools and checks thumbnails against srctools + Pillow.

Usage: python3 validate.py <work_dir> <thumb command...>
  The thumb command gets "<in.vtf> <maxSize> <alphaMode> <out.bgra>" appended and must
  print "w h alpha [mip]" (tests/thumb_dump or the Wine fake_explorer wrapper).
"""
import io
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image
from srctools.vtf import VTF, ImageFormats, VTFFlags, CubeSide

work = Path(sys.argv[1])
cmd = sys.argv[2:]
work.mkdir(parents=True, exist_ok=True)
failures = 0


def check(ok, name, detail=""):
    global failures
    failures += not ok
    print(f"{'OK  ' if ok else 'FAIL'} {name:44} {detail}")


def thumb(path, size, alpha_mode=0):
    out = work / "out.bgra"
    r = subprocess.run(cmd + [str(path), str(size), str(alpha_mode), str(out)], capture_output=True, text=True)
    line = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "ERROR (no output) " + r.stderr[-200:]
    if r.returncode != 0 or line.startswith("ERROR"):
        return None, line
    f = line.split()
    w, h, a = int(f[0]), int(f[1]), int(f[2])
    mip = int(f[3]) if len(f) > 3 else None
    raw = out.read_bytes()
    if len(raw) != w * h * 4:
        return None, f"bad size {len(raw)} for {w}x{h}"
    rgba = bytearray(raw)
    rgba[0::4], rgba[2::4] = raw[2::4], raw[0::4]
    return Image.frombytes("RGBA", (w, h), bytes(rgba)), (w, h, a, mip)


def pattern(w, h, seed=0, alpha=True):
    px = bytearray(w * h * 4)
    i = 0
    for y in range(h):
        for x in range(w):
            px[i] = (x * 255 // max(1, w - 1) + seed * 40) & 255
            px[i + 1] = (y * 255 // max(1, h - 1) + seed * 20) & 255
            px[i + 2] = ((x ^ y) * 3 + seed * 60) & 255
            px[i + 3] = (255 if (x // 16 + y // 16) % 2 else 40 + (x & 127)) if alpha else 255
            i += 4
    return bytes(px)


def make(name, w, h, fmt=ImageFormats.BGRA8888, alpha=True, frames=1, flags=VTFFlags(0), version=(7, 5),
         cubemap=False):
    v = VTF(w, h, version=version, frames=frames, fmt=fmt, flags=flags)
    if cubemap:
        for i, s in enumerate(v._depth_range()):
            v.get(side=s).copy_from(pattern(w, h, i, alpha))
    else:
        for fr in range(frames):
            v.get(frame=fr).copy_from(pattern(w, h, fr, alpha))
    p = work / f"{name}.vtf"
    with p.open("wb") as f:
        v.save(f)
    return p


def reference(path, mip, frame=0, side=None):
    v = VTF.read(io.BytesIO(path.read_bytes()))
    v.load()
    kw = {"frame": frame, "mipmap": mip}
    if side is not None:
        kw["side"] = side
    return v.get(**kw).to_PIL().convert("RGBA")


def diff(a, b, rgb_only=False, min_alpha=0):
    """Max channel difference; RGB ignored where alpha < min_alpha."""
    if a.size != b.size:
        return 999
    worst = 0
    pa, pb = a.tobytes(), b.tobytes()
    for i in range(0, len(pa), 4):
        if pb[i + 3] < min_alpha:
            continue
        for c in range(3 if rgb_only else 4):
            worst = max(worst, abs(pa[i + c] - pb[i + c]))
    return worst


def area(img, dw, dh):
    """Exact area-average downscale with alpha weighting (Pillow's BOX samples pixel
    centres instead of integrating coverage, so it is not a usable reference)."""
    w, h = img.size
    src = img.tobytes()

    def cover(n, d):
        out = []
        for i in range(d):
            a, b = i * n / d, (i + 1) * n / d
            taps = []
            s = int(a)
            while s < n and s < b:
                c = min(b, s + 1) - max(a, s)
                if c > 1e-9:
                    taps.append((s, c))
                s += 1
            out.append(taps)
        return out

    xs, ys = cover(w, dw), cover(h, dh)
    out = bytearray(dw * dh * 4)
    for y in range(dh):
        for x in range(dw):
            acc = [0.0] * 4
            for sy, cy in ys[y]:
                for sx, cx in xs[x]:
                    i = (sy * w + sx) * 4
                    wa = cx * cy * src[i + 3] / 255
                    acc[0] += src[i] * wa; acc[1] += src[i + 1] * wa; acc[2] += src[i + 2] * wa; acc[3] += wa
            total = (w / dw) * (h / dh)
            o = (y * dw + x) * 4
            if acc[3] > 1e-9:
                for c in range(3):
                    out[o + c] = min(255, int(acc[c] / acc[3] + 0.5))
                out[o + 3] = min(255, int(acc[3] / total * 255 + 0.5))
    return Image.frombytes("RGBA", (dw, dh), bytes(out))


def opaque(img):
    r, g, b, _ = img.split()
    return Image.merge("RGBA", (r, g, b, Image.new("L", img.size, 255)))


# --- mip selection: an exact mip needs no scaling -----------------------------------
p = make("rgba_1024", 1024, 1024)
for size, mip in [(256, 2), (1024, 0), (2048, 0), (32, 5)]:
    img, info = thumb(p, size, 1)
    ok = img is not None and info[:2] == (size if size <= 1024 else 1024,) * 2 and info[2] == 1 and \
        info[3] in (None, mip) and diff(img, reference(p, mip)) == 0
    check(ok, f"1024 BGRA8888 -> {size} (mip {mip})", str(info))

# Request between two mips: next bigger mip, box-filtered down.
for size, mip in [(96, 3), (200, 2), (300, 1)]:
    img, info = thumb(p, size, 1)
    ref = area(reference(p, mip), size, size)
    d = diff(img, ref) if img else 999
    check(img is not None and info[3] in (None, mip) and d <= 1, f"1024 BGRA8888 -> {size} (mip {mip} + box)",
          f"{info} max_diff={d}")

# --- formats (256x256 -> 64 = mip 2) ------------------------------------------------
formats = {"DXT1": 2, "DXT3": 2, "DXT5": 2, "BGRA8888": 0, "RGBA8888": 0, "ABGR8888": 0, "BGR888": 0,
           "RGB888": 0, "BGR565": 1, "RGB565": 1, "BGRA4444": 0, "BGRA5551": 1, "I8": 0, "IA88": 0}
for name, tol in formats.items():
    try:
        # DXT1 without alpha: srctools ignores DXT1's 1-bit transparency, the engine (and we) don't.
        p = make("fmt_" + name, 256, 256, ImageFormats[name], alpha=name != "DXT1")
    except Exception as e:
        print(f"skip {name}: {e}")
        continue
    img, info = thumb(p, 64, 1)
    ref = reference(p, 2)
    d = diff(img, ref) if img else 999
    check(img is not None and img.size == (64, 64) and d <= tol, f"format {name}", f"{info} max_diff={d}")

# --- shapes -------------------------------------------------------------------------
p = make("wide_512x128", 512, 128, ImageFormats.DXT1, alpha=False)
img, info = thumb(p, 256)
d = diff(img, reference(p, 1)) if img else 999
check(img is not None and img.size == (256, 64) and info[2] == 0 and d <= 2, "512x128 DXT1 -> 256x64 (mip 1)",
      f"{info} max_diff={d}")

p = make("tall_64x256", 64, 256, ImageFormats.BGR888, alpha=False)
img, info = thumb(p, 100)
ref = area(reference(p, 1), 25, 100)
d = diff(img, ref) if img else 999
check(img is not None and img.size == (25, 100) and d <= 1, "64x256 -> 25x100", f"{info} max_diff={d}")

p = make("tiny_32", 32, 32, ImageFormats.BGRA8888)
img, info = thumb(p, 256, 1)
check(img is not None and img.size == (32, 32) and diff(img, reference(p, 0)) == 0, "32x32 -> 32x32 (no upscale)",
      str(info))

# No mipmaps, not a power of two, version 7.2 header built by hand.
W, H = 300, 200
hdr = bytearray(80)
hdr[0:4] = b"VTF\0"
struct.pack_into("<IIIHHIHH", hdr, 4, 7, 2, 80, W, H, VTFFlags.NO_MIP.value, 1, 0)
struct.pack_into("<f", hdr, 48, 1.0)
struct.pack_into("<iBiBBH", hdr, 52, 2, 1, -1, 0, 0, 1)  # RGB888, 1 mip, no low-res image
src = Image.frombytes("RGBA", (W, H), pattern(W, H, 3, alpha=False)).convert("RGB")
p = work / "npot_nomip.vtf"
p.write_bytes(bytes(hdr) + src.tobytes())
img, info = thumb(p, 256)
ref = area(src.convert("RGBA"), 256, 171)
d = diff(img, ref) if img else 999
check(img is not None and img.size == (256, 171) and d <= 1, "300x200 no mips -> 256x171", f"{info} max_diff={d}")

# --- versions -----------------------------------------------------------------------
for ver in [(7, 2), (7, 3), (7, 4), (7, 5)]:
    p = make(f"ver_{ver[1]}", 128, 128, ImageFormats.DXT5, version=ver)
    img, info = thumb(p, 64, 1)
    d = diff(img, reference(p, 1)) if img else 999
    check(img is not None and d <= 2, f"version 7.{ver[1]}", f"{info} max_diff={d}")

# --- alpha modes --------------------------------------------------------------------
p_col = make("alpha_color", 64, 64, ImageFormats.BGRA8888)
p_nrm = make("alpha_normal", 64, 64, ImageFormats.BGRA8888, flags=VTFFlags.NORMAL)
p_opq = make("opaque", 64, 64, ImageFormats.BGRA8888, alpha=False)
for path, mode, want in [(p_col, 0, 1), (p_nrm, 0, 0), (p_nrm, 1, 1), (p_col, 2, 0), (p_opq, 0, 0), (p_opq, 1, 0)]:
    img, info = thumb(path, 64, mode)
    ref = reference(path, 0)
    if not want:
        ref = opaque(ref)
    ok = img is not None and info[2] == want and diff(img, ref) == 0
    check(ok, f"alpha mode {mode} on {path.stem}", f"alpha={info[2] if img else info}")

# --- animation / cubemap: first frame / face -----------------------------------------
p = make("anim3", 64, 64, ImageFormats.BGRA8888, frames=3)
img, info = thumb(p, 64, 1)
check(img is not None and diff(img, reference(p, 0, frame=0)) == 0, "animated: frame 0", str(info))
for ver in [(7, 5), (7, 4)]:
    p = make(f"cube_{ver[1]}", 64, 64, ImageFormats.BGRA8888, flags=VTFFlags.ENVMAP, version=ver, cubemap=True)
    img, info = thumb(p, 32, 1)
    ref = reference(p, 1, side=list(CubeSide)[0])
    check(img is not None and diff(img, ref) == 0, f"cubemap v7.{ver[1]}: face 0, mip 1", str(info))

# --- partial reading: only the header and one mip level are needed ---------------------
good = (work / "rgba_1024.vtf").read_bytes()
p = work / "cut_after_mip2.vtf"
p.write_bytes(good[: len(good) // 3])  # mips are stored smallest first
img, info = thumb(p, 256, 1)
check(img is not None and diff(img, reference(work / "rgba_1024.vtf", 2)) == 0, "file cut after mip 2 -> 256 still works",
      str(info))

# --- broken files: an error, never a crash --------------------------------------------
huge = bytearray(good[:80])
struct.pack_into("<HH", huge, 16, 16384, 16384)
bad = {"empty": b"", "not_vtf": b"PNG" + b"\0" * 100, "garbage": b"VTF\0" + b"\xff" * 200,
       "truncated_header": good[:40], "truncated_in_mip": good[:200000],
       "header_size_huge": good[:12] + b"\xff\xff\xff\x7f" + good[16:4096],
       "huge_dimensions": bytes(huge) + good[80:200000]}
for name, blob in bad.items():
    p = work / f"bad_{name}.vtf"
    p.write_bytes(blob)
    img, info = thumb(p, 256)
    check(img is None and str(info).startswith("ERROR"), f"reject {name}", str(info))

print("\nfailures:", failures)
sys.exit(1 if failures else 0)

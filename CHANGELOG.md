# Changelog

## 1.0.0

First version.

- Thumbnails for `.vtf` (VTF 7.0 – 7.5) in Windows Explorer, 64-bit, no external DLLs.
- Formats: DXT1/3/5, DXT1 with 1-bit alpha, ATI1N/ATI2N, all 8-bit RGB(A)/BGR(A)/ABGR/ARGB/BGRX,
  565/5551/4444, I8/IA88/A8, UV88/UVWQ/UVLX, bluescreen, HDR 16/32-bit (clamped).
- Reads only the header and one mip level (the smallest one that is still big enough), so large
  textures are fast; the image is area-averaged down to the requested size.
- Transparency for textures with alpha, except normal / SSBump maps (alpha is a mask there);
  `AlphaMode` registry setting.
- Animated textures show frame 1, cubemaps the first face.
- `regsvr32` registration for all users (administrator) or the current user; another program's
  `.vtf` thumbnail handler is restored on unregistration.

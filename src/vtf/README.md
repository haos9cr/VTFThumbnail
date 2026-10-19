# VTF reader

Dependency-free decoder for VTF 7.0 – 7.5. It can parse the header, locate a single mip level
of one frame and decode just that, which is what the thumbnail provider needs.

It started as the reader of [VTFExport](https://github.com/haos9cr/VTFExport) (the Photoshop
plug-in) and is maintained separately here; the two projects don't depend on each other.

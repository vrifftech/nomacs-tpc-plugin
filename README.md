# nomacs TPC Image Plugin

A Qt 6 image-format plugin for viewing BioWare TPC textures from *Star Wars:
Knights of the Old Republic* and *Knights of the Old Republic II*. Because
nomacs loads standard formats through `QImageReader`, installing this plugin in
nomacs' Qt `imageformats` directory makes `.tpc` files available to the normal
viewer and thumbnail pipeline.

## Current support

- PC TPC header validation
- Uncompressed grayscale, RGB, and RGBA
- BGRA encoding `0x0C`, including xoreos-compatible de-swizzling
- BC1 / DXT1
- BC3 / DXT5
- Top-level mip image
- First face of compressed cube maps
- First frame of TXI `proceduretype cycle` animations
- Embedded TXI exposed as image description metadata
- Alpha transparency
- Odd BC image dimensions and truncated-input checks
- Conversion from TPC's bottom-left file orientation to Qt's top-left orientation

Not yet supported: animated playback, six-face cube-map presentation,
mip-level selection, or TPC writing. Animated and cube textures display their
first frame or face.

## Build

Install a C++17 compiler, CMake 3.21+, and the Qt 6 Core, Gui, and Test
development packages. Build with the same Qt toolchain used by nomacs.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Windows, add `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64` (adjusted for
your installation) when configuring.

## Install for nomacs

Copy the built plugin into the `imageformats` directory used by nomacs and
restart nomacs. Typical layouts are:

```text
Windows: <nomacs directory>/imageformats/qtpc.dll
Linux:   <qt plugin directory>/imageformats/libqtpc.so
macOS:   <nomacs.app>/Contents/PlugIns/imageformats/libqtpc.dylib
```

The plugin and nomacs must use ABI-compatible Qt builds. To diagnose discovery:

```sh
QT_DEBUG_PLUGINS=1 nomacs --list-formats
```



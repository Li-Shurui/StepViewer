# StepViewer

**English** | [简体中文](README.zh-CN.md)

StepViewer is a Qt Widgets desktop application for opening, inspecting, and (when print support is available) printing documents and images. It is a **plugin-based viewer**: the host window does not decode file formats itself. Instead it loads viewer plugins at runtime and hands each file to the plugin that claims it.

The project is especially useful for **headerless raw images** (YUV, packed RGB, Bayer mosaics, grayscale dumps) that JPEG/PNG viewers cannot interpret. Those files carry no width, height, or pixel format in a header, so the YUV plugin combines the file name, saved session state, and toolbar choices to decide how to read the bytes.

---

## Origin: modified from the official Qt Document Viewer demo

**This project is not an original application from scratch.** It is a modified and extended version of the official Qt example **Document Viewer** (`demos/documentviewer`).

| Official demo | This project |
| --- | --- |
| Name: **Document Viewer** | Name: **StepViewer** |
| Example path: `demos/documentviewer` | This repository |
| Typical purpose: JSON, text, images, PDF, optional 3D | Same plugin host, plus a large raw-image (YUV) plugin; PDF plugin is not shipped here |
| Documentation: [Document Viewer \| Qt 6](https://doc.qt.io/qt-6/qtdoc-demos-documentviewer-example.html) | This README and `doc/md/` |

The upstream demo shows how to:

- Build a `QMainWindow` with static and dynamic menus, toolbars, and actions
- Load viewer plugins with `QPluginLoader` (static and shared)
- Persist preferences and recent files with `QSettings`
- Localize the UI (English and Simplified Chinese in the Qt example)

Source files throughout `app/` and several plugins still carry **The Qt Company Ltd.** copyright headers and the SPDX identifier `LicenseRef-Qt-Commercial OR BSD-3-Clause`. The original Qt documentation lives in this tree as `doc/src/documentviewer.qdoc`.

What this fork changes relative to that demo is summarized under [Differences from the Qt demo](#differences-from-the-qt-demo).

---

## Features

### Host application (`StepViewer`)

- **Open files** from the File menu (`Ctrl+O`), the recent-files list, the command line, or drag-and-drop (local files, and on Windows some `FileContents` clipboard payloads).
- **Automatic viewer selection** by file extension, then MIME type; files without an extension can go to a raw-data viewer.
- **Manual viewer override** via the **Mode** menu (text / JSON / image / YUV) after a file is already open.
- **Recent files** stored with `QSettings` (default cap: 10).
- **Language**: English and Simplified Chinese, switchable at runtime under **Help → Language**. Each plugin loads its own translation files.
- **Theme**: dark stylesheet (bundled [QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet)) or the default Qt style, under **Help → Theme**. The choice is saved.
- **Printing** when Qt Print Support is present (`DOCUMENTVIEWER_PRINTSUPPORT`).
- **Overview sidebar** (west-side tabs): plugins that support it add Info, bookmarks, histogram, and similar pages.
- **Busy cursor** while a viewer runs background work (load, decode, histogram).
- If **no plugins** can be loaded, the process shows an error and exits.

### Viewer plugins

| Plugin | Role |
| --- | --- |
| **TxtViewer** | Plain text (`text/plain`). Edit, cut/copy/paste, save, save as, print. Also accepts any file when chosen explicitly from Mode (binary files will look like garbage). |
| **JsonViewer** | JSON (`application/json`) as a tree (`QTreeView` + custom `QAbstractItemModel`). Bookmarks, expand/collapse all, print. Parsing can run on a worker thread. |
| **ImageViewer** | Formats supported by `QImageReader` (PNG, JPEG, BMP, WebP, … depending on the Qt build). Zoom, Info tab, color-space conversion toward sRGB for display, print. Allocation limit raised to 1024 MB. |
| **YuvViewer** | Headerless raw frames: YUV, packed RGB, Bayer, Y8. Layout toolbar, multi-frame sequences, plane view, pixel probe, histogram, display transforms, PNG/BMP export. See [YUV / raw image viewer](#yuv--raw-image-viewer). |
| **Q3DViewer** | Built only if `Qt6::Quick3D` is found. Displays 3D assets (including `.mesh`) in a `QQuickView`. |

The official Qt demo also includes a **PdfViewer** plugin. That plugin is **not** part of this repository.

---

## Differences from the Qt demo

These are the main product and architecture changes on top of `demos/documentviewer`:

1. **Rebranded** to StepViewer (window title, organization/application names used by `QSettings`, icon, executable `StepViewer` / `stepviewer`).
2. **New plugin: YuvViewer** — the bulk of the custom work: 39 raw pixel formats, file-name layout parsing, OpenCV conversion, display-only transforms, and inspection tools aimed at camera/ISP dumps.
3. **Mode menu** — force Txt / JSON / Image / YUV on the current file instead of relying only on MIME/extension.
4. **Dark theme** via QDarkStyleSheet; default theme is dark.
5. **AbstractViewer** extended with:
   - Worker-thread tasks (`startAsyncTask` / `startAsyncTaskWithProgress`) and cooperative cancel
   - Chunked file reads with progress
   - Overview helpers `addInfoTab()` / `addTabPage()`
   - C++23 `std::expected` in public headers
6. **ViewerFactory** extended with `namedViewer()`, `acceptsAnyFile()`, `supportsExtensionlessFiles()`, and extension-based matching (needed for raw dumps).
7. **No PdfViewer** in this tree.
8. **C++23** required for the shared library and several plugins.
9. **OpenCV** (`core` + `imgproc`) for YUV/RGB/Bayer conversion; if a system OpenCV is missing, CMake can FetchContent OpenCV 4.12.0 (core/imgproc only).
10. Internal design notes for the YUV plugin live under `doc/md/` (Chinese).

---

## Requirements

| Item | Requirement |
| --- | --- |
| Qt | **6.8 or later** (`qt_standard_project_setup(REQUIRES 6.8)`). Components: **Core, Gui, Widgets, Concurrent, LinguistTools**. Optional: **PrintSupport**, **Quick3D**. |
| Compiler | C++23 (`std::expected`, and the YUV plugin also uses it throughout). |
| CMake | 3.16 or later. |
| OpenCV | Optional at configure time. If `OpenCV` (modules `core` and `imgproc`) is found, it is used; otherwise a **minimal bundled OpenCV 4.12.0** is fetched (needs Git and network on first configure). |
| OS | Windows, macOS, and Linux are supported by the CMake install/plugin-path logic. |

Shared-library Qt builds load plugins from a `plugins` directory next to the executable (see [Plugin loading](#plugin-loading)). Static Qt builds link plugins into the application.

---

## Building

Configure against a Qt 6.8+ prefix, then build:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-Qt6>
cmake --build build
```

On Windows with a specific generator, for example:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build
```

Install (optional; also runs `qt_generate_deploy_app_script` so Qt runtime libraries can be collected):

```bash
cmake --install build --prefix <install-prefix>
```

CMake project name: `StepViewer` version `1.0`. The executable target is `stepviewer` with output name **StepViewer**.

If OpenCV is not installed, the first configure of `plugins/yuvviewer` may clone OpenCV 4.12.0. That step is slow and requires network access. Installing a system OpenCV with `core` and `imgproc` avoids the fetch.

---

## Running

```text
StepViewer [options] [File]
```

| Argument | Meaning |
| --- | --- |
| `-h` / `--help` | Help |
| `-v` / `--version` | Version (`1.0`) |
| positional `File` | Path opened at startup |

Typical locations after a non-install build:

- Windows: `build/app/StepViewer.exe` with plugins under `build/plugins/` (or `build/app/plugins/` depending on generator). The factory looks for `plugins` next to the executable, or `../plugins` for an installed layout.
- macOS installed bundle: `StepViewer.app/Contents/PlugIns`
- Linux: `../plugins` relative to the binary for both installed and non-installed layouts used in this project.

If plugins are missing, you will see **No viewer plugins found** and the process exits with code 1.

---

## User interface

The main window (`app/mainwindow.ui`) is a splitter:

- **Left:** a west-oriented `QTabWidget` for plugin overview pages (Info, Histogram, bookmarks, …). Empty placeholder tabs from the `.ui` file (“Pages” / “Bookmarks”) are the original demo layout; plugins replace or add pages after they initialize.
- **Right:** a `QScrollArea` that hosts the active viewer’s widget.

### Menus

| Menu | Contents |
| --- | --- |
| **File** | Open (`Ctrl+O`), Recent files, Print (`Ctrl+P`, enabled when the viewer can print), Exit (`Ctrl+Q`) |
| **Mode** | Force **text / json / image / yuv** viewer for the current file |
| **Help** | About StepViewer (`Ctrl+H`), About Qt (`Ctrl+I`), **Language**, **Theme** |

Toolbar: Open, Recent, Print, Back / Forward (for viewers that use them). The YUV plugin also injects layout controls onto the Open toolbar and a second bar for view/plane/frame.

Settings keys (organization/application `StepViewer`): working directory, window geometry, per-viewer binary state, recent files, theme.

---

## Architecture

High-level flow:

```text
main.cpp
  └─ MainWindow
        ├─ ViewerFactory  ── loads plugins (QPluginLoader)
        │      └─ ViewerInterface (Qt plugin IID)
        │             └─ AbstractViewer
        │                    ├─ TxtViewer / JsonViewer / ImageViewer / YuvViewer / Q3DViewer
        └─ QSettings, RecentFiles, Translator, theme stylesheet
```

### `AbstractViewer`

Shared library `abstractviewer` (`app/abstractviewer.*`). It owns:

- The current `QFile`
- Menus and toolbars the plugin added to the main window (removed in `cleanup()`)
- Overview tab pages
- Translation (`Translator` + `QEvent::LanguageChange`)
- Async work: a generation counter discards superseded results; `cancelAsyncTasks()` is cooperative

Viewer instances are **reused**. Opening another file calls `cleanup()` then `init()`; the plugin object is not destroyed. Plugins must treat toolbar/tab pointers as invalid after `cleanup()` (`YuvViewer` uses `QPointer` for that).

### `ViewerInterface`

Defined in `app/viewerinterfaces.h`:

```text
IID: org.qt-project.Qt.Examples.DocumentViewer.ViewerInterface/1.0
```

Each plugin uses `Q_PLUGIN_METADATA` and a small JSON file (`"Keys": [ "…" ]`).

### Viewer selection (`ViewerFactory`)

1. Match **file suffix** against `supportedExtensions()`.
2. If the suffix is empty, offer viewers with `supportsExtensionlessFiles()` (YUV).
3. Else match **MIME type** (`QMimeDatabase`).
4. Else fall back to the default viewer (TxtViewer, depending on `DefaultPolicy`).

`namedViewer()` is used by the Mode menu. Viewers with `acceptsAnyFile() == true` (YUV and text) may take a file the factory would not auto-select.

### Plugin loading

Static plugin instances are registered first, then shared libraries from:

| Platform | Search |
| --- | --- |
| macOS | `../PlugIns` (installed) or `../../../../plugins` (dev) |
| Windows | `plugins` beside the exe, else `../plugins` |
| Other | `../plugins` |

---

## YUV / raw image viewer

This is the main addition over the Qt demo. Design notes (Chinese) are in `doc/md/00-architecture.md` through `09-modularization.md`.

### Why it exists

A `.nv12` or `.raw` file is a bag of bytes. The same buffer interpreted as NV12 vs I420 is two different pictures. The plugin never “sniffs” a magic header; it **interprets** the file using:

| Fact | Priority (high → low) |
| --- | --- |
| Pixel format | Extension (`.nv12`, `.Y8`, …) → last session → default **NV12** |
| Width / height | File name (`w[1920]_h[1080]` or `1920x1080`) → last session → default **1920×1080** |
| Stride / scanline | `_stride[N]` / `_scanline[N]` in the name (invalid numbers are an error) → user-edited toolbar values → compact layout of the current format. If a compact frame divides the file size and the name did not declare padding, the stride/scanline boxes stay **read-only**. |
| 16-bit sample packing | Format convention (e.g. P010 = 10-bit MSB-aligned) → last session → full-range 16-bit |
| Display transform | Last session → **Linear** (pixels as decoded) |

A **clear file extension always wins over session state** for format and sample packing: opening `.Y8` must not reuse a previous I420 layout (that used to index chroma planes that do not exist).

If the name has no size, the UI prompts you to fill width/height and press **Reload**. A name that *looks* like it has dimensions but cannot be parsed is an error.

### File-name conventions

Implemented in `plugins/yuvviewer/rawimagefilename.cpp`:

1. **Key/value dump names**, for example  
   `p[MfsrBlend0]_…_[out]_port[9]_w[1920]_h[1440]_stride[1920]_scanline[1440]`  
   Keys are normalized (`p` → pipeline, `w`/`h` → width/height, `[out]_port` → output, …) and shown on the Info tab.
2. **WxH somewhere in the name**, for example `capture_1920x1080.yuv`. This sets only width and height, not stride.

### Supported pixel formats (39)

YUV conversion uses OpenCV `imgproc`. Bayer demosaic uses OpenCV Bayer→RGB constants (note: OpenCV names CFA phases by the **second row’s second and third columns**, not the top-left 2×2, so RGGB maps to `COLOR_BayerBG2RGB`, and similarly for the other phases). Bayer conversion is **demosaic only** (no black level, white balance, CCM, or gamma in the decoder).

#### YUV 4:2:0

| Display name | Layout | Typical extensions |
| --- | --- | --- |
| NV12 | Semi-planar Y + interleaved UV | `.nv12`, `.YUV420NV12` |
| NV21 | Semi-planar Y + interleaved VU | `.nv21`, `.yuv420sp`, `.YUV420NV21` |
| I420 | Planar Y, U, V | `.i420`, `.yuv420p`, `.yu12`, `.iyuv` |
| YV12 | Planar Y, V, U | `.yv12` |
| P010 | Semi-planar 10-bit in 16-bit words, MSB aligned | `.p010` |
| P016 | Semi-planar 16-bit | `.p016` |
| I010 | Planar 10-bit in 16-bit words, MSB aligned | `.i010` |
| I016 | Planar 16-bit | `.i016` |

Generic `.yuv` / `.YUV` is also registered so “unknown YUV” files still open this plugin; you then pick the real format in the combo box.

#### YUV 4:2:2

| Display name | Layout | Typical extensions |
| --- | --- | --- |
| YUY2 | Packed YUYV | `.yuy2`, `.yuyv` |
| UYVY | Packed UYVY | `.uyvy` |
| YVYU | Packed YVYU | `.yvyu` |
| VYUY | Packed VYUY | `.vyuy` |
| I422 | Planar Y, U, V | `.i422`, `.yuv422p` |
| YV16 | Planar Y, V, U | `.yv16` |
| NV16 | Semi-planar Y + UV | `.nv16` |
| NV61 | Semi-planar Y + VU | `.nv61` |

#### YUV 4:4:4

| Display name | Layout | Typical extensions |
| --- | --- | --- |
| I444 | Planar | `.i444`, `.yuv444p` |
| YV24 | Planar Y, V, U | `.yv24` |
| NV24 | Semi-planar | `.nv24` |
| NV42 | Semi-planar VU | `.nv42` |
| YUV444 | Packed YUV | `.yuv444`, `.iyu2`, `.v308` |
| AYUV | Packed AYUV / VUYA | `.ayuv`, `.vuya` |

#### Packed RGB and gray

| Display name | Notes | Typical extensions |
| --- | --- | --- |
| RGB888 / BGR888 | 24-bit | `.rgb888`, `.rgb` / `.bgr888`, `.bgr` |
| RGBA8888 / RGBX8888 / BGRA8888 / BGRX8888 | 32-bit; X is not alpha | `.rgba`, `.rgbx`, `.bgra`, … |
| RGB565 / BGR565 / RGB555 / BGR555 | 16-bit packed | `.rgb565`, … |
| RGB48 / RGBA64 | 16-bit per channel, little-endian | `.rgb48`, `.rgba64` |
| Y8 | 8-bit gray | `.y8` |

#### Bayer (16-bit mosaics)

| Display name | Typical extensions |
| --- | --- |
| Bayer RGGB16 | `.rggb16`, `.raw` |
| Bayer GRBG16 | `.grbg16` |
| Bayer GBRG16 | `.gbrg16` |
| Bayer BGGR16 | `.bggr16` |

Plane view for Bayer shows the four CFA phases (R / Gr / Gb / B) at half width and half height, **without** interpolation.

### Layout model

A frame is uniquely described by `(decoder, data, layout)`:

- `width`, `height` — visible size  
- `stride` — **bytes per row of the first plane** (may include row padding)  
- `scanline` — **row count of the first plane** (may include plane padding)  
- `sample` — for 16-bit containers: significant bits and MSB vs LSB alignment  

4:2:0 requires even width and height (and even stride/scanline as validated). 4:2:2 requires even width/stride. Dimensions are clamped to 2…32768.

**16-bit packing** matters: 10-bit LSB data read as full-range 16-bit occupies ~1.5% of the range and looks black. The Sample combo box is enabled only for formats that store samples in 16-bit words.

### Multi-frame files

If `fileSize % frameSize == 0` and the quotient is greater than 1, the file is a sequence:

- Frame spin box (1-based) and `/ N` label become active  
- Only the selected frame is read (`seek` to `frameIndex * frameSize`)  
- File size that is **not** a multiple of the current layout is an error (the message includes layout parameters so you can fix format or size)

### Format guess from file size

With a known width/height, the format combo **bolds** formats whose **compact** frame size divides the file size. Tooltips show bytes per frame and frame count. This does **not** auto-switch the format. Padded files will not match this heuristic.

### Display (does not change samples)

Presets on the **View** combo (implemented as auto-level / gray-world / gamma):

| Preset | Meaning |
| --- | --- |
| Linear | Decoded values as-is (default) |
| Gamma 2.2 | Encoding gamma after gains |
| Auto level | Stretch using a high percentile (avoids hot pixels setting the gain) |
| Auto + Gamma 2.2 | Both |
| Auto + WB + Gamma 2.2 | Auto-level, gray-world white balance, then gamma |

Pixel probe and histogram always read **raw samples**. Export saves **what is on screen** (after the display transform).

### Inspection tools

- **Plane** combo: Composite, then Y/U/V or R/G/B/A/X at native (possibly subsampled) resolution. Switching planes does not re-read the file.
- **Pixel probe**: mouse over the image; status bar shows composite coordinates and native samples, e.g. `Y=128 U=90 V=200`. On 10/16-bit formats the numbers are native range (0…1023 or 0…65535), not the 8-bit preview.
- **Histogram** tab: per-component 256-bin charts with means; computed on a worker thread from planes, not from the RGB preview.
- **Info** tab: resolved layout, decoder, frame size / file size / frame count, and file-name metadata.
- **Zoom**: nearest-neighbor by default (inspect samples); optional smooth scaling; pixel grid when each source pixel is at least 4 screen pixels; fit to window.
- **Export** (`Ctrl+E`): current view to PNG or BMP. Suggested names include frame and plane, e.g. `capture_frame3_u.png`. BMP drops alpha. High-bit-depth sources export the 8-bit (or converted) display image.

### YUV keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+R` | Reload with current toolbar layout |
| `Ctrl++` / `Ctrl+-` | Zoom in / out |
| `Ctrl+0` | Reset zoom |
| `Ctrl+9` | Fit to window |
| `Ctrl+E` | Export PNG/BMP |

Frame number is edited in the toolbar spin box (the design notes mention PgUp/PgDn; the current code exposes `stepFrame()` for that purpose).

### Threading and safety

- Disk read, decode, and histogram run on a thread pool; the UI thread only presents pixmaps.
- Display-preset changes re-transform the cached decoded `QImage` without a full reload, on a separate generation counter so they do not cancel an in-flight load.
- **`m_loadedDecoder` vs `m_decoder`**: plane view and the probe only use the decoder that actually produced `m_rawData`. If you change the format combo, you must Reload.
- **`RawImageFrame::validate()`** must succeed before indexing buffers (wrong format + right-sized buffer would otherwise read out of bounds).
- Load path catches `std::bad_alloc` and other exceptions and turns them into UI errors.
- OpenCV SIMD is disabled in this plugin on Windows (`cv::setUseOptimized(false)`) because some MinGW AVX2 YUV paths have been observed to fault.

### Adding a format

1. Subclass a family in `yuvdecoders.cpp`, `rgbdecoders.cpp`, or `bayerdecoders.cpp` (or `RawImageDecoder` plus helpers in `rawimagedecoder_p.h`).
2. Register it in the corresponding `create*Decoders()` factory.

`YuvViewer` has no per-format `switch`.

Module layout:

```text
yuvviewer          orchestration (load, zoom, export, state)
  yuvcontrols      toolbar
  yuvimagewidget   paint / zoom / grid
    rawimagedisplay, rawimagehistogram, rawimagefilename, rawimageframe
      rawimagedecoder + yuv/rgb/bayer decoders
```

---

## Internationalization

| Target | Translation base | Files |
| --- | --- | --- |
| Application + `abstractviewer` | `docviewer` | `app/docviewer_en.ts`, `app/docviewer_zh_CN.ts` (merged with `qtbase`) |
| Txt / JSON / Image / YUV / Q3D | per-plugin | `*_en.ts`, `*_zh_CN.ts` |

CMake declares `I18N_SOURCE_LANGUAGE en` and `I18N_TRANSLATED_LANGUAGES zh_CN`. Runtime: `Help → Language`, or a system locale change. Plugins listen for `QEvent::LanguageChange` and call `retranslate()`.

---

## Directory map

```text
.
├── CMakeLists.txt              Top-level project (StepViewer)
├── LICENSE                     GNU AGPL v3 (see License)
├── README.md                   This file (English)
├── README.zh-CN.md             Chinese README
├── app/                        Host executable + AbstractViewer library
│   ├── main.cpp
│   ├── mainwindow.*            UI, settings, drag-and-drop, Mode/Theme
│   ├── viewerfactory.*         Plugin discovery and selection
│   ├── viewerinterfaces.h      Plugin IID
│   ├── abstractviewer.*        Shared viewer API
│   ├── translator.*            QTranslator wrapper
│   ├── recentfiles.* / recentfilemenu.*
│   ├── documentviewer.qrc      Icons (paths still use demos/documentviewer)
│   └── qdarkstyle/             Dark QSS
├── plugins/
│   ├── txtviewer/
│   ├── jsonviewer/
│   ├── imageviewer/
│   ├── yuvviewer/              Raw image plugin (this fork’s main feature)
│   └── Q3DViewer/              Optional Quick3D plugin
└── doc/
    ├── src/documentviewer.qdoc Original Qt example documentation
    └── md/                     YUV plugin design notes (Chinese)
        ├── 00-architecture.md
        ├── 01-multi-frame-navigation.md
        ├── 02-plane-viewing.md
        ├── 03-pixel-probe.md
        ├── 04-zoom-improvements.md
        ├── 05-format-guess-from-size.md
        ├── 06-export-png-bmp.md
        ├── 07-histogram-tab.md
        ├── 08-high-bit-depth.md
        └── 09-modularization.md
```

---

## License and third-party notices

Please treat licensing as **mixed** and read the files that actually ship:

1. **Qt Document Viewer demo and many source files**  
   Copyright (C) The Qt Company Ltd.  
   SPDX: `LicenseRef-Qt-Commercial OR BSD-3-Clause`  
   See the [official example](https://doc.qt.io/qt-6/qtdoc-demos-documentviewer-example.html) and Qt’s example licensing.

2. **Repository root `LICENSE`**  
   GNU Affero General Public License v3. If you distribute this tree as a whole, follow that file and the notices in individual sources.

3. **QDarkStyleSheet**  
   Bundled under `app/qdarkstyle/` (stylesheet generated by qtsass). See the [upstream project](https://github.com/ColinDuquesnoy/QDarkStyleSheet) for its license.

4. **OpenCV**  
   Used by YuvViewer (`core`, `imgproc`). If fetched by CMake, it is OpenCV 4.12.0 under Apache-2.0.

This software is provided as-is; see the license texts for warranty disclaimers.

---

## Further reading

- Qt official demo: [Document Viewer](https://doc.qt.io/qt-6/qtdoc-demos-documentviewer-example.html)
- Qt plugins: [How to Create Qt Plugins](https://doc.qt.io/qt-6/plugins-howto.html) (the Document Viewer is the running example for `ViewerInterface`)
- YUV plugin internals: `doc/md/00-architecture.md`

# StepViewer

[English](README.md) | **简体中文**

StepViewer 是一个基于 Qt Widgets 的桌面程序，用于打开、查看，并在具备打印支持时打印文档和图像。它是**插件式查看器**：主窗口本身不解码具体格式，而是在运行时加载插件，把文件交给声称支持该类型的插件。

本项目特别适合查看 **没有文件头的裸图**（YUV、打包 RGB、Bayer 马赛克、灰度 dump）。JPEG/PNG 查看器无法解读这类文件：宽高和像素格式都不在文件头里。YUV 插件会综合**文件名、上次会话状态、工具栏上的选择**来决定如何解释这堆字节。

---

## 来源：由 Qt 官方 demo 修改而来

**本项目不是从零写的独立应用。** 它是在 Qt 官方示例 **Document Viewer**（`demos/documentviewer`）基础上修改和扩展得到的。

| 官方 demo | 本项目 |
| --- | --- |
| 名称：**Document Viewer** | 名称：**StepViewer** |
| 示例路径：`demos/documentviewer` | 本仓库 |
| 典型用途：JSON、文本、图像、PDF，以及可选的 3D | 同一套插件宿主，并新增大型裸图（YUV）插件；**未包含**官方 demo 中的 PDF 插件 |
| 文档：[Document Viewer \| Qt 6](https://doc.qt.io/qt-6/qtdoc-demos-documentviewer-example.html) | 本 README 以及 `doc/md/` |

上游 demo 演示了如何：

- 使用带静态/动态菜单、工具栏和 Action 的 `QMainWindow`
- 用 `QPluginLoader` 加载查看器插件（静态与动态）
- 用 `QSettings` 保存偏好和最近打开的文件
- 做界面本地化（官方示例同样提供英文和简体中文）

`app/` 以及多个插件源文件仍带有 **The Qt Company Ltd.** 的版权声明，以及 SPDX 标识 `LicenseRef-Qt-Commercial OR BSD-3-Clause`。官方示例文档以 `doc/src/documentviewer.qdoc` 的形式保留在本仓库中。

相对官方 demo 的改动见下文 [与 Qt 官方 demo 的差异](#与-qt-官方-demo-的差异)。

---

## 功能概览

### 宿主程序（`StepViewer`）

- **打开文件**：文件菜单（`Ctrl+O`）、最近文件列表、命令行参数、拖放（本地文件；在 Windows 上还支持部分 `FileContents` 剪贴板数据）。
- **自动选择查看器**：先按扩展名，再按 MIME 类型；没有扩展名的文件可以交给裸数据查看器。
- **手动指定查看器**：打开文件后，用 **Mode（模式）** 菜单强制使用文本 / JSON / 图像 / YUV。
- **最近文件**：用 `QSettings` 保存（默认最多 10 条）。
- **语言**：英文与简体中文，可在运行时通过 **帮助 → 语言** 切换。每个插件各自加载自己的翻译。
- **主题**：**帮助 → 主题** 可选深色样式表（内置 [QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet)）或 Qt 默认样式，选择会写入设置。默认是深色。
- **打印**：若编译时存在 Qt Print Support（宏 `DOCUMENTVIEWER_PRINTSUPPORT`）。
- **左侧概览栏**（西侧标签）：支持概览的插件可加入信息页、书签、直方图等。
- 查看器在后台加载/解码时显示**忙碌光标**。
- **找不到任何插件**时弹出错误并退出进程。

### 查看器插件

| 插件 | 作用 |
| --- | --- |
| **TxtViewer** | 纯文本（`text/plain`）。编辑、剪切/复制/粘贴、保存、另存为、打印。在模式菜单中显式选中时也可打开任意文件（二进制会显示成乱码）。 |
| **JsonViewer** | JSON（`application/json`），以树显示（`QTreeView` + 自定义 `QAbstractItemModel`）。书签、全部展开/折叠、打印。解析可在工作线程进行。 |
| **ImageViewer** | `QImageReader` 支持的格式（PNG、JPEG、BMP、WebP 等，取决于 Qt 构建）。缩放、信息页、向 sRGB 做色彩空间转换以便显示、打印。图像分配上限提高到 1024 MB。 |
| **YuvViewer** | 无头裸帧：YUV、打包 RGB、Bayer、Y8。布局工具栏、多帧序列、分平面、像素探针、直方图、仅影响显示的变换、导出 PNG/BMP。详见 [YUV / 裸图查看器](#yuv--裸图查看器)。 |
| **Q3DViewer** | 仅在找到 `Qt6::Quick3D` 时编译。在 `QQuickView` 中显示 3D 资源（含 `.mesh`）。 |

官方 Document Viewer demo 还包含 **PdfViewer**。**本仓库未包含该插件。**

---

## 与 Qt 官方 demo 的差异

在 `demos/documentviewer` 之上，本仓库主要做了这些产品和架构改动：

1. **更名为 StepViewer**（窗口标题、`QSettings` 使用的组织名/应用名、图标、可执行文件名 `StepViewer` / `stepviewer`）。
2. **新增插件 YuvViewer**：自定义工作的主体。39 种裸像素格式、从文件名解析布局、OpenCV 转换、仅显示层变换，以及面向相机/ISP dump 的检查工具。
3. **Mode 菜单**：对当前文件强制使用文本 / JSON / 图像 / YUV，而不只依赖 MIME/扩展名。
4. **深色主题**（QDarkStyleSheet），默认启用深色。
5. **扩展 AbstractViewer**：
   - 工作线程任务（`startAsyncTask` / `startAsyncTaskWithProgress`）与协作式取消
   - 带进度的分块读文件
   - 概览页助手 `addInfoTab()` / `addTabPage()`
   - 公共头文件使用 C++23 的 `std::expected`
6. **扩展 ViewerFactory**：`namedViewer()`、`acceptsAnyFile()`、`supportsExtensionlessFiles()`，以及按扩展名匹配（裸 dump 需要）。
7. **本树中没有 PdfViewer。**
8. 共享库和多个插件要求 **C++23**。
9. YUV 转换依赖 **OpenCV**（`core` + `imgproc`）；若系统找不到 OpenCV，CMake 可用 FetchContent 拉取 OpenCV 4.12.0（仅 core/imgproc）。
10. YUV 插件的设计说明在 `doc/md/`（中文）。

---

## 环境要求

| 项目 | 要求 |
| --- | --- |
| Qt | **6.8 或更高**（`qt_standard_project_setup(REQUIRES 6.8)`）。组件：**Core、Gui、Widgets、Concurrent、LinguistTools**。可选：**PrintSupport**、**Quick3D**。 |
| 编译器 | C++23（`std::expected`；YUV 插件全程使用）。 |
| CMake | 3.16 或更高。 |
| OpenCV | 配置时可选。若找到带 `core`、`imgproc` 的 OpenCV 则使用；否则会 **拉取精简的 OpenCV 4.12.0**（首次配置需要 Git 和网络）。 |
| 操作系统 | CMake 中的安装路径与插件搜索覆盖 Windows、macOS、Linux。 |

Qt 动态库构建时，插件从可执行文件旁的 `plugins` 目录加载（见 [插件加载](#插件加载)）。静态 Qt 构建会把插件链进主程序。

---

## 编译

指定 Qt 6.8+ 的安装前缀后配置并编译：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt6路径>
cmake --build build
```

Windows 上指定生成器的例子：

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build
```

安装（可选；会运行 `qt_generate_deploy_app_script` 以便收集 Qt 运行库）：

```bash
cmake --install build --prefix <安装前缀>
```

CMake 工程名为 `StepViewer`，版本 `1.0`。可执行目标是 `stepviewer`，输出名为 **StepViewer**。

若未安装 OpenCV，配置 `plugins/yuvviewer` 时可能克隆 OpenCV 4.12.0，耗时长且需要联网。安装带 `core`、`imgproc` 的系统 OpenCV 可跳过拉取。

---

## 运行

```text
StepViewer [选项] [文件]
```

| 参数 | 含义 |
| --- | --- |
| `-h` / `--help` | 帮助 |
| `-v` / `--version` | 版本（`1.0`） |
| 位置参数 `文件` | 启动时打开该路径 |

非安装构建后的常见位置：

- Windows：`build/app/StepViewer.exe`，插件在 `build/plugins/`（或随生成器变为 `build/app/plugins/`）。工厂会在可执行文件旁找 `plugins`，安装布局则找 `../plugins`。
- macOS 安装包：`StepViewer.app/Contents/PlugIns`
- Linux：相对二进制的 `../plugins`（本项目的安装与未安装布局均如此）

插件缺失时会提示 **No viewer plugins found**（无法加载查看器插件），并以退出码 1 结束。

---

## 用户界面

主窗口（`app/mainwindow.ui`）是水平分割：

- **左侧：** 西侧标签的 `QTabWidget`，给插件放概览页（信息、直方图、书签等）。`.ui` 里原有的「Pages / Bookmarks」占位来自官方 demo；插件初始化后会替换或追加页面。
- **右侧：** `QScrollArea`，放当前查看器的控件。

### 菜单

| 菜单 | 内容 |
| --- | --- |
| **文件** | 打开（`Ctrl+O`）、最近文件、打印（`Ctrl+P`，查看器可打印时才启用）、退出（`Ctrl+Q`） |
| **Mode（模式）** | 对当前文件强制使用 **text / json / image / yuv** 查看器 |
| **帮助** | 关于 StepViewer（`Ctrl+H`）、关于 Qt（`Ctrl+I`）、**语言**、**主题** |

工具栏：打开、最近、打印、后退 / 前进（供使用它们的查看器）。YUV 插件还会把布局控件挂到「打开」那一行工具栏，并另起一行放显示/平面/帧号。

设置键（组织名/应用名均为 `StepViewer`）：工作目录、窗口几何、各查看器的二进制状态、最近文件、主题。

---

## 架构

高层数据流：

```text
main.cpp
  └─ MainWindow
        ├─ ViewerFactory  ── 加载插件（QPluginLoader）
        │      └─ ViewerInterface（Qt 插件 IID）
        │             └─ AbstractViewer
        │                    ├─ TxtViewer / JsonViewer / ImageViewer / YuvViewer / Q3DViewer
        └─ QSettings、RecentFiles、Translator、主题样式表
```

### `AbstractViewer`

共享库 `abstractviewer`（`app/abstractviewer.*`），负责：

- 当前 `QFile`
- 插件挂到主窗口上的菜单和工具栏（在 `cleanup()` 里拆除）
- 概览标签页
- 翻译（`Translator` + `QEvent::LanguageChange`）
- 异步任务：用代数丢弃过期结果；`cancelAsyncTasks()` 是协作式取消

查看器实例会被 **复用**。换文件走 `cleanup()` 再 `init()`，插件对象本身不销毁。插件必须把工具栏/标签页指针在 `cleanup()` 之后视为无效（`YuvViewer` 为此使用 `QPointer`）。

### `ViewerInterface`

定义在 `app/viewerinterfaces.h`：

```text
IID: org.qt-project.Qt.Examples.DocumentViewer.ViewerInterface/1.0
```

每个插件使用 `Q_PLUGIN_METADATA` 以及一个小 JSON 文件（`"Keys": [ "…" ]`）。

### 查看器选择（`ViewerFactory`）

1. 用文件后缀匹配 `supportedExtensions()`。
2. 后缀为空时，交给 `supportsExtensionlessFiles()` 为真的查看器（YUV）。
3. 否则用 **MIME 类型**（`QMimeDatabase`）匹配。
4. 再否则回退到默认查看器（通常是 TxtViewer，取决于 `DefaultPolicy`）。

模式菜单走 `namedViewer()`。`acceptsAnyFile() == true` 的查看器（YUV 和文本）可以打开工厂本来不会自动选中的文件。

### 插件加载

先注册静态插件实例，再从下列目录加载动态库：

| 平台 | 搜索路径 |
| --- | --- |
| macOS | `../PlugIns`（安装版）或 `../../../../plugins`（开发构建） |
| Windows | 可执行文件旁的 `plugins`，否则 `../plugins` |
| 其他 | `../plugins` |

---

## YUV / 裸图查看器

这是相对 Qt demo 的主要新增部分。设计说明见 `doc/md/00-architecture.md` 至 `09-modularization.md`。

### 存在的原因

`.nv12` 或 `.raw` 只是一串字节。同一块缓冲按 NV12 还是 I420 解释，会得到两张完全不同的图。插件**不会**靠魔数嗅探文件头，而是按下表 **解释** 文件：

| 信息 | 优先级（高 → 低） |
| --- | --- |
| 像素格式 | 扩展名（`.nv12`、`.Y8` 等）→ 上次会话 → 默认 **NV12** |
| 宽 / 高 | 文件名（`w[1920]_h[1080]` 或 `1920x1080`）→ 上次会话 → 默认 **1920×1080** |
| stride / scanline | 文件名中的 `_stride[N]` / `_scanline[N]`（数字不合法则直接报错）→ 用户改过的工具栏值 → 当前格式的紧凑排布。若紧凑帧大小能整除文件、且文件名没写 padding，stride/scanline 框保持 **只读**。 |
| 16 位样本打包 | 格式惯例（如 P010 = 10 bit 左对齐）→ 上次会话 → 满量程 16 bit |
| 显示变换 | 上次会话 → **Linear（线性）**（按解码结果原样显示） |

**明确的扩展名始终压过会话里保存的格式和样本打包**：打开 `.Y8` 绝不能沿用上次的 I420 布局（那样会去索引根本不存在的色度平面）。

文件名里没有尺寸时，界面会提示填写宽高并按 **Reload**。文件名**看起来**带了尺寸却解析失败，则视为错误。

### 文件名约定

实现位于 `plugins/yuvviewer/rawimagefilename.cpp`：

1. **键值 dump 命名**，例如  
   `p[MfsrBlend0]_…_[out]_port[9]_w[1920]_h[1440]_stride[1920]_scanline[1440]`  
   键会做归一化（`p` → pipeline，`w`/`h` → width/height，`[out]_port` → output 等），并显示在信息页。
2. **名字某处的 WxH**，例如 `capture_1920x1080.yuv`。这只提供宽高，不锁定 stride。

### 支持的像素格式（39 种）

YUV 转换使用 OpenCV `imgproc`。Bayer 去马赛克使用 OpenCV 的 Bayer→RGB 常量（注意：OpenCV 按**第二行的第二、三列**命名 CFA 相位，不是左上角 2×2，因此 RGGB 对应 `COLOR_BayerBG2RGB`，其余相位类似）。Bayer 转换**只做去马赛克**（解码器不做黑电平、白平衡、颜色矩阵或 Gamma）。

#### YUV 4:2:0

| 显示名 | 布局 | 常见扩展名 |
| --- | --- | --- |
| NV12 | 半平面 Y + 交错 UV | `.nv12`、`.YUV420NV12` |
| NV21 | 半平面 Y + 交错 VU | `.nv21`、`.yuv420sp`、`.YUV420NV21` |
| I420 | 平面 Y、U、V | `.i420`、`.yuv420p`、`.yu12`、`.iyuv` |
| YV12 | 平面 Y、V、U | `.yv12` |
| P010 | 半平面，10 bit 放在 16 bit 字的高位 | `.p010` |
| P016 | 半平面 16 bit | `.p016` |
| I010 | 平面，10 bit 放在 16 bit 字的高位 | `.i010` |
| I016 | 平面 16 bit | `.i016` |

通用 `.yuv` / `.YUV` 也会注册到本插件，以便「不知道具体格式的 YUV」仍能打开，再在下拉框里选真正的格式。

#### YUV 4:2:2

| 显示名 | 布局 | 常见扩展名 |
| --- | --- | --- |
| YUY2 | 打包 YUYV | `.yuy2`、`.yuyv` |
| UYVY | 打包 UYVY | `.uyvy` |
| YVYU | 打包 YVYU | `.yvyu` |
| VYUY | 打包 VYUY | `.vyuy` |
| I422 | 平面 Y、U、V | `.i422`、`.yuv422p` |
| YV16 | 平面 Y、V、U | `.yv16` |
| NV16 | 半平面 Y + UV | `.nv16` |
| NV61 | 半平面 Y + VU | `.nv61` |

#### YUV 4:4:4

| 显示名 | 布局 | 常见扩展名 |
| --- | --- | --- |
| I444 | 平面 | `.i444`、`.yuv444p` |
| YV24 | 平面 Y、V、U | `.yv24` |
| NV24 | 半平面 | `.nv24` |
| NV42 | 半平面 VU | `.nv42` |
| YUV444 | 打包 YUV | `.yuv444`、`.iyu2`、`.v308` |
| AYUV | 打包 AYUV / VUYA | `.ayuv`、`.vuya` |

#### 打包 RGB 与灰度

| 显示名 | 说明 | 常见扩展名 |
| --- | --- | --- |
| RGB888 / BGR888 | 24 bit | `.rgb888`、`.rgb` / `.bgr888`、`.bgr` |
| RGBA8888 / RGBX8888 / BGRA8888 / BGRX8888 | 32 bit；X 不是 alpha | `.rgba`、`.rgbx`、`.bgra` 等 |
| RGB565 / BGR565 / RGB555 / BGR555 | 16 bit 位域打包 | `.rgb565` 等 |
| RGB48 / RGBA64 | 每通道 16 bit，小端 | `.rgb48`、`.rgba64` |
| Y8 | 8 bit 灰度 | `.y8` |

#### Bayer（16 bit 马赛克）

| 显示名 | 常见扩展名 |
| --- | --- |
| Bayer RGGB16 | `.rggb16`、`.raw` |
| Bayer GRBG16 | `.grbg16` |
| Bayer GBRG16 | `.gbrg16` |
| Bayer BGGR16 | `.bggr16` |

Bayer 的分平面视图显示四个 CFA 相位（R / Gr / Gb / B），半宽半高，**不做插值**。

### 布局模型

一帧由三元组 `(decoder, data, layout)` 唯一确定：

- `width`、`height` — 可见尺寸  
- `stride` — **首平面每行字节数**（可含行填充）  
- `scanline` — **首平面行数**（可含面填充）  
- `sample` — 对 16 bit 容器：有效位数，以及高位对齐还是低位对齐  

4:2:0 要求宽高为偶数（以及校验中的偶数 stride/scanline）。4:2:2 要求宽和 stride 为偶数。尺寸限制为 2…32768。

**16 bit 打包方式很关键**：把右对齐的 10 bit 数据当成满量程 16 bit 读，取值大约只占量程的 1.5%，看起来就是一张黑图。Sample 下拉框只对把样本放进 16 bit 字的格式启用。

### 多帧文件

若 `fileSize % frameSize == 0` 且商大于 1，则视为序列：

- 启用帧号数字框（从 1 起）和 `/ N` 标签  
- 只读取当前帧（`seek` 到 `frameIndex * frameSize`）  
- 文件大小**不能**被当前布局整除时直接报错（信息里带布局参数，便于改格式或尺寸）

### 按文件大小反推格式

已知宽高时，格式下拉框会把「**紧凑**帧大小能整除文件大小」的格式显示为**粗体**。悬停提示每帧字节数和总帧数。这**不会**自动切换当前格式。带 padding 的文件不会命中该启发式。

### 显示变换（不改采样值）

**View** 下拉框中的预设（由自动电平 / 灰世界 / Gamma 组合而成）：

| 预设 | 含义 |
| --- | --- |
| Linear | 按解码值原样显示（默认） |
| Gamma 2.2 | 在增益之后做编码 Gamma |
| Auto level | 按高分位拉伸（避免热像素定增益） |
| Auto + Gamma 2.2 | 两者结合 |
| Auto + WB + Gamma 2.2 | 自动电平、灰世界白平衡，再 Gamma |

像素探针和直方图始终读 **原始样本**。导出保存的是 **屏幕上看到的图**（经过显示变换）。

### 检查工具

- **Plane** 下拉框：合成图，然后是 Y/U/V 或 R/G/B/A/X，分辨率为该平面原生尺寸（色度可能已子采样）。切换平面不重新读盘。
- **像素探针**：鼠标移到图像上，状态栏显示合成坐标和原生样本，例如 `Y=128 U=90 V=200`。10/16 bit 格式报的是原生量程（0…1023 或 0…65535），不是 8 bit 预览值。
- **Histogram（直方图）** 页：按分量 256 bin，带均值；在工作线程从平面统计，不从 RGB 预览图统计。
- **Info（信息）** 页：解析后的布局、解码器、帧大小 / 文件大小 / 帧数，以及文件名元数据。
- **缩放**：默认最近邻（方便看采样）；可选平滑缩放；源像素至少占 4 个屏幕像素时叠加像素网格；适应窗口。
- **导出**（`Ctrl+E`）：当前视图存为 PNG 或 BMP。建议文件名带帧号和平面，例如 `capture_frame3_u.png`。BMP 会丢掉 alpha。高 bit 深度源导出的是转换后的 8 bit（或已转换的）显示图。

### YUV 快捷键

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl+R` | 用当前工具栏布局重新加载 |
| `Ctrl++` / `Ctrl+-` | 放大 / 缩小 |
| `Ctrl+0` | 重置缩放 |
| `Ctrl+9` | 适应窗口 |
| `Ctrl+E` | 导出 PNG/BMP |

帧号在工具栏数字框中修改（设计文档提到过 PgUp/PgDn；当前代码提供 `stepFrame()` 供步进）。

### 线程与安全

- 读盘、解码、直方图在线程池中执行；UI 线程只负责把结果变成屏幕上的图。
- 切换显示预设只对缓存的解码 `QImage` 做变换，不完整重载，并用单独的代数，以免取消正在进行的加载。
- **`m_loadedDecoder` 与 `m_decoder` 分开**：分平面和探针只用真正读出 `m_rawData` 的那个解码器。改了格式下拉框必须 Reload。
- 索引缓冲区前必须通过 **`RawImageFrame::validate()`**（格式选错但缓冲大小碰巧合法时，否则会越界读）。
- 加载路径捕获 `std::bad_alloc` 等异常，转成界面错误，而不是让进程崩溃。
- 本插件在 Windows 上关闭 OpenCV SIMD（`cv::setUseOptimized(false)`），因为部分 MinGW AVX2 YUV 路径曾出现故障。

### 增加一种格式

1. 在 `yuvdecoders.cpp`、`rgbdecoders.cpp` 或 `bayerdecoders.cpp` 里继承某个族基类（或直接继承 `RawImageDecoder`，并用 `rawimagedecoder_p.h` 里的原语）。
2. 在对应的 `create*Decoders()` 工厂里注册。

`YuvViewer` 没有任何按格式分支的 `switch`。

模块划分：

```text
yuvviewer          编排（加载、缩放、导出、状态）
  yuvcontrols      工具栏
  yuvimagewidget   绘制 / 缩放 / 网格
    rawimagedisplay、rawimagehistogram、rawimagefilename、rawimageframe
      rawimagedecoder 以及 yuv/rgb/bayer 解码器
```

---

## 国际化

| 目标 | 翻译基名 | 文件 |
| --- | --- | --- |
| 主程序 + `abstractviewer` | `docviewer` | `app/docviewer_en.ts`、`app/docviewer_zh_CN.ts`（合并 `qtbase`） |
| 文本 / JSON / 图像 / YUV / Q3D | 各插件自己的基名 | `*_en.ts`、`*_zh_CN.ts` |

CMake 声明源语言为 `en`，翻译语言为 `zh_CN`。运行时通过 **帮助 → 语言** 切换，或跟随系统语言变更。插件监听 `QEvent::LanguageChange` 并调用 `retranslate()`。

---

## 目录结构

```text
.
├── CMakeLists.txt              顶层工程（StepViewer）
├── LICENSE                     GNU AGPL v3（见许可）
├── README.md                   英文说明
├── README.zh-CN.md             本文件（中文）
├── app/                        宿主可执行文件 + AbstractViewer 库
│   ├── main.cpp
│   ├── mainwindow.*            界面、设置、拖放、模式/主题
│   ├── viewerfactory.*         插件发现与选择
│   ├── viewerinterfaces.h      插件 IID
│   ├── abstractviewer.*        查看器公共 API
│   ├── translator.*            QTranslator 封装
│   ├── recentfiles.* / recentfilemenu.*
│   ├── documentviewer.qrc      图标（资源路径仍使用 demos/documentviewer）
│   └── qdarkstyle/             深色 QSS
├── plugins/
│   ├── txtviewer/
│   ├── jsonviewer/
│   ├── imageviewer/
│   ├── yuvviewer/              裸图插件（本 fork 的主要功能）
│   └── Q3DViewer/              可选的 Quick3D 插件
└── doc/
    ├── src/documentviewer.qdoc Qt 官方示例文档原稿
    └── md/                     YUV 插件设计说明（中文）
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

## 许可与第三方说明

许可情况是**混合的**，请以实际随附文件为准：

1. **Qt Document Viewer demo 以及大量源文件**  
   Copyright (C) The Qt Company Ltd.  
   SPDX：`LicenseRef-Qt-Commercial OR BSD-3-Clause`  
   详见 [官方示例](https://doc.qt.io/qt-6/qtdoc-demos-documentviewer-example.html) 以及 Qt 对示例的许可说明。

2. **仓库根目录的 `LICENSE`**  
   GNU Affero General Public License v3。若将本仓库作为整体分发，请同时遵守该文件以及各源文件中的声明。

3. **QDarkStyleSheet**  
   位于 `app/qdarkstyle/`（由 qtsass 生成的样式表）。许可见 [上游项目](https://github.com/ColinDuquesnoy/QDarkStyleSheet)。

4. **OpenCV**  
   YuvViewer 使用 `core`、`imgproc`。若由 CMake 拉取，则为 OpenCV 4.12.0（Apache-2.0）。

本软件按原样提供；免责声明见各许可证文本。

---

## 延伸阅读

- Qt 官方 demo：[Document Viewer](https://doc.qt.io/qt-6/qtdoc-demos-documentviewer-example.html)
- Qt 插件机制：[How to Create Qt Plugins](https://doc.qt.io/qt-6/plugins-howto.html)（Document Viewer 是其中 `ViewerInterface` 的完整示例）
- YUV 插件内部设计：`doc/md/00-architecture.md`

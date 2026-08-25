# YUV Viewer 架构

本文是 `plugins/yuvviewer` 的总览，说明模块怎么划分、一个文件从磁盘到
屏幕经过哪些步骤，以及往哪里加东西。功能层面的细节见 `01`–`09` 各篇。

## 在应用中的位置

YUV Viewer 是一个 Qt 插件，实现 `ViewerInterface`（`app/viewerinterfaces.h`），
由 `ViewerFactory` 用 `QPluginLoader` 加载，公共行为继承自 `AbstractViewer`。

它和其他 viewer 有一点本质不同：**裸图文件没有文件头**。JPEG 能自报宽高
和像素格式，`.nv12` 不能——同一串字节按 NV12 还是 I420 解释，出来是两张
完全不同的图。所以本插件的大部分复杂度不在解码，而在**判断该怎么解释这
堆字节**，并在判断可能出错的前提下不越界读内存。

因为格式和尺寸由用户在工具栏里指定，本插件重写了
`acceptsAnyFile()` 返回 `true`：被显式选中时它接受任何文件，自己负责
判断内容是否讲得通，而不是让 factory 靠扩展名拦在外面。

## 模块划分

| 文件 | 职责 |
| --- | --- |
| `rawimagedecoder.h/.cpp` | 解码器抽象基类 + 读盘（`readData()`）+ 格式注册表 |
| `rawimagedecoder_p.h` | 各解码器共用的原语：布局校验、平面提取、像素描述 |
| `yuvdecoders.cpp` | YUV 各族解码器（4:2:0 / 4:2:2 / 4:4:4，8 与 16 bit） |
| `rgbdecoders.cpp` | 单平面格式：packed RGB、位打包 RGB565 系列、灰度 Y8 |
| `rawimagefilename.h/.cpp` | 从文件名解析布局与元数据 |
| `rawimageframe.h/.cpp` | 帧一致性校验 + 渲染（合成图或单平面） |
| `rawimagehistogram.h/.cpp` | 逐通道直方图统计与绘图 |
| `yuvimagewidget.h/.cpp` | 显示控件：缩放、平滑/最近邻、像素网格 |
| `yuvcontrols.h/.cpp` | 工具栏控件：尺寸、格式、平面、帧号 |
| `yuvviewer.h/.cpp` | 编排：生命周期、状态、加载流程、缩放、导出 |

依赖方向是单向的，从下往上：

```
yuvviewer  ───────────────┐  编排层，依赖以下全部
  yuvcontrols             │  UI 层
  yuvimagewidget         ─┤  （只依赖 Qt）
    rawimagehistogram     │  纯逻辑层，无 UI、可在工作线程调用
    rawimagefilename      │
    rawimageframe        ─┤
      rawimagedecoder     │  解码层
      rawimagedecoder_p  ─┘
```

下层不知道上层的存在：`rawimage*` 四个模块不含任何 widget 代码，也不引用
`YuvViewer`，因此都能在工作线程里直接调用。OpenCV 只出现在解码层
（外加 `yuvviewer.cpp` 里一行关闭优化路径的调用）。

## 核心数据模型

一帧的解释由一个三元组唯一确定：

```cpp
struct RawImageLayout { int width, height, stride, scanline; };
```

- `width`/`height` 是图像尺寸；
- `stride` 是首平面**每行的字节数**，可以大于宽度（行填充）；
- `scanline` 是首平面**实际行数**，可以大于高度（面填充）。

`stride`/`scanline` 描述首平面，各格式在自己的
`validateLayout()` / `expectedByteSize()` 里决定色度平面怎么推导。

贯穿全插件的关键三元组是 **(decoder, data, layout)**。所有按坐标索引缓冲区
的操作（`convertToImage`、`extractPlane`、`describePixel`）都假设三者自洽，
一旦不自洽就是越界读。见下面的"安全约束"。

## 打开一个文件

裸图没有头，所以"该怎么解释"有三个信息源，按优先级合并：

| 信息 | 优先级（高到低） |
| --- | --- |
| 像素格式 | 扩展名（`.Y8`/`.nv12`…）> 上次会话保存的格式 > 默认 NV12 |
| 宽高 | 文件名（`w[..]_h[..]` 或 `1920x1080`）> 上次会话 > 默认 1920×1080 |
| stride/scanline | 文件名（仅当用户没改动宽高时）> 紧凑排布 |

格式一栏里扩展名压过会话状态，是必须的：`.Y8` 文件套用上次会话的 I420
会去读根本不存在的色度平面。

具体时序：

1. `init()` 建显示控件和工具栏，按扩展名选解码器，用
   `RawImageFileName::layout()` / `metadata()` 解析文件名；
2. `AbstractViewer` 发 `uiInitialized`，`setupYuvUi()` 建"Info"和
   "Histogram"两个概览页，若文件名给出了布局就请求加载；
3. `MainWindow` 紧接着调 `restoreState()`，恢复格式、帧号、平面，
   必要时补上宽高，再请求一次加载；
4. 两次请求由 `requestReload()` 合并（详见 `09`），文件只读一遍；
5. `reload()` 同步做布局校验和帧数计算，然后把读盘 + 解码 + 直方图
   丢给工作线程；
6. 完成回调缓存原始字节，显示图像，填充信息页和直方图页。

文件名解析不出尺寸也不是错误——此时显示提示语，等用户在工具栏填完
宽高后按 Reload。只有当文件名**看着像**带了尺寸却解析不出来（例如
只有 `_w[..]` 没有 `_h[..]`）才算错误并提示。

## 解码器体系

`RawImageDecoder` 是无状态单例，由注册表持有。接口分三组：

```cpp
// 身份
QLatin1StringView id() const;      // 稳定键，存进会话状态
QString displayName() const;       // 下拉框显示
QStringList fileExtensions() const;

// 布局
LayoutResult validateLayout(const RawImageLayout &) const;  // 基类只查范围
int defaultStride(int width) const;                          // 紧凑行宽
qint64 expectedByteSize(const RawImageLayout &) const;

// 像素
ImageResult convertToImage(const QByteArray &, const RawImageLayout &) const;
QStringList planeNames() const;                    // 空表示不支持分平面
ImageResult extractPlane(...) const;
QString describePixel(...) const;                  // 像素探针
```

错误统一用 `std::expected<T, QString>` 返回，消息直接面向用户——裸图的
失败几乎都是"你选的格式和这个文件对不上"，需要说清哪里对不上。

读盘由基类的 `readData()` 统一实现：按 `expectedByteSize()` 校验文件大小、
分块读取并上报进度、支持协作式取消，文件大小是帧大小整数倍时按多帧序列
处理。

### 格式族

30 个格式按族组织，族基类装布局规则和转换，具体格式只填分量顺序和命名：

| 族基类 | 文件 | 具体格式 |
| --- | --- | --- |
| `SemiPlanarYuv420Decoder` | `yuvdecoders.cpp` | NV12, NV21 |
| `SemiPlanarYuv420p16Decoder` | `yuvdecoders.cpp` | P010, P016 |
| `PlanarYuv420Decoder` | `yuvdecoders.cpp` | I420, YV12 |
| `PlanarYuv420p16Decoder` | `yuvdecoders.cpp` | I010, I016 |
| `PackedYuv422Decoder` | `yuvdecoders.cpp` | YUY2, UYVY, YVYU |
| `PlanarYuv422Decoder` | `yuvdecoders.cpp` | I422, YV16 |
| `SemiPlanarYuv422Decoder` | `yuvdecoders.cpp` | NV16, NV61 |
| `PlanarYuv444Decoder` | `yuvdecoders.cpp` | I444, YV24 |
| `PackedRgbDecoder` | `rgbdecoders.cpp` | RGB888, BGR888, RGBA8888, RGBX8888, BGRA8888, BGRX8888, RGB565, BGR565, RGB555, BGR555 |
| `PackedRgb16Decoder` | `rgbdecoders.cpp` | RGB48, RGBA64 |
| （直接继承） | `rgbdecoders.cpp` | Y8 |

`rawimagedecoder_p.h` 提供的可复用原语：

- 布局校验：`validateYuv420Layout()`（宽高/stride/scanline 均须偶数）、
  `validateYuv422Layout()`（只约束宽和 stride 偶数）；
- 转换封装：`rgbaMatToImage()`（cv::Mat → 脱离的 QImage）、
  `runConversion()`（把 OpenCV 异常翻成错误结果）；
- 平面提取：`grayscalePlane()` / `stridedPlane()` / `rgb16Plane()` /
  `grayscale16Plane()` / `strided16Plane()`；
- 像素描述：`describeYuv()` / `describeRgb()` / `describeRgba()`。

### 加一个新格式

1. 在 `yuvdecoders.cpp` 或 `rgbdecoders.cpp` 里挑一个族基类继承，
   只需重写 `id()` / `displayName()` / `mimeType()` / `fileExtensions()`
   和描述分量顺序的那一两个钩子；族里没有合适的就直接继承
   `RawImageDecoder`，用 `rawimagedecoder_p.h` 的原语拼出实现；
2. 在同文件末尾的 `createYuvDecoders()` / `createRgbDecoders()` 里加一行。

注册表 `RawImageDecoders::all()` 把两个工厂的结果拼起来，列表顺序即格式
下拉框的顺序。`YuvViewer` 不含任何格式相关的分支，无需改动。

## 线程模型

读盘、解码、直方图统计全在工作线程，UI 线程只做显示：

- `AbstractViewer::startAsyncTaskWithProgress()` 起 `QtConcurrent` 任务，
  带进度回调和完成回调，完成回调回到 UI 线程；
- `QPainter` 画到 `QImage` 是线程安全的，所以直方图连绘图一起在工作线程
  完成，UI 线程只做一次 `QPixmap::fromImage()`（`QPixmap` 必须在 GUI 线程）；
- 取消是协作式的：`readData()` 每读完一块就问一次进度回调要不要继续，
  新的加载会先 `cancelAsyncTasks()` 作废旧的；
- 切换平面**不重新读盘**，直接对缓存的 `m_rawData` 做提取（`QByteArray`
  隐式共享，捕获进 lambda 的代价可忽略）。

`cancelAsyncTasks()` 只作废回调、不阻塞等待，所以切换文件时 UI 不卡；
真正的阻塞等待放在 `~AbstractViewer()`，那时对象即将销毁。

## 生命周期与状态

viewer 实例由 `ViewerFactory` 长期持有并**复用**：换文件走
`cleanup()` → `init()`，对象本身不销毁。这带来两条约束：

- `cleanup()` 会删掉工具栏、概览页等挂在主窗口下的东西，所以
  `YuvViewer` 里指向它们的成员一律是 `QPointer`，删除后自动置空；
  `YuvControls` 挂在工具栏名下当子对象，跟着工具栏一起走；
- `cleanup()` 必须释放帧缓冲。一帧 4096×3072 就是十几 MB，而实例可能
  在整个进程生命周期里都活着。

会话状态（`saveState()` / `restoreState()`）用 `QDataStream` 依次写入
viewer 名、宽、高、格式 id、帧号、平面索引。字段是逐版本追加的，
读取时用 `atEnd()` 判断，旧状态文件仍可读。格式存的是 `id()` 字符串而非
下拉框序号，所以调整格式顺序不会让旧状态错位。

## 安全约束

裸图的失败模式和别的 viewer 不一样：格式选错不会"解码失败"，而是**按错
误的尺寸去索引一块合法内存**，读出越界数据或直接段错误。曾经真实发生过
一次：`.Y8` 文件套用了上次会话保存的 I420 布局，像素探针去读并不存在的
色度平面。

因此有两条硬约束：

1. **`m_loadedDecoder` 与 `m_decoder` 分开。** 前者是 `m_rawData` 实际
   用哪个解码器读出来的，后者是用户当前选的。切换平面和像素探针只认
   `m_loadedDecoder`，两者不一致时提示重新加载而不是硬解。
2. **索引缓冲区前必须过 `RawImageFrame::validate()`。**
   它检查 (decoder, data, layout) 三元组自洽，`render()` 内部会先调它，
   像素探针和直方图渲染也各自调一次——即使当前调用点恰好安全，也不依赖
   调用顺序来保证安全。

此外加载任务包了 `std::bad_alloc` / `std::exception` / `...` 三层
catch，把内存不足和 OpenCV 内部异常翻成用户可见的错误，而不是让插件
带着整个应用一起退出。

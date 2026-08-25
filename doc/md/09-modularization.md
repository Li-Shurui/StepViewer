# 模块化重构

把 YUV Viewer 从两个巨型文件拆成按职责划分的模块，同时修掉重构过程中
暴露出来的若干潜在风险。**没有新增功能，也没有改变任何既有行为。**

架构现状见 `00-architecture.md`；本篇只记录这次改了什么、为什么。

## 动机

重构前整个插件只有两个实现文件：

| 文件 | 行数 | 内容 |
| --- | --- | --- |
| `yuvviewer.cpp` | 1342 | 文件名解析、帧校验、直方图统计与绘图、显示控件、工具栏搭建、加载编排、缩放、导出、信息页 |
| `rawimagedecoder.cpp` | 2056 | 基类实现 + 共享原语 + 30 个格式的解码器 + 注册表 |

唯一被抽出来的抽象是 `RawImageDecoder`，其余逻辑全堆在 `YuvViewer` 里。
后果是：

- 想改直方图的坐标轴，得在一个 1342 行的文件里翻到中段的自由函数；
- 显示控件 `YuvImageWidget` 定义在 `.cpp` 中段，绘制逻辑和 viewer 的
  业务逻辑混在一起；
- `YuvViewer` 有 42 个成员变量，其中 12 个是工具栏 widget 裸指针，
  每处使用都要自己判空，判漏了也看不出来；
- 文件名解析、帧校验、直方图这些**不含 UI 的纯逻辑**用的是
  `YuvViewer::tr()`，反向依赖 widget 类。

## 拆分结果

`yuvviewer.cpp` 1342 → 807 行，`rawimagedecoder.cpp` 2056 → 180 行。

### 从 `yuvviewer.cpp` 抽出

| 新模块 | 行数 | 抽出的内容 |
| --- | --- | --- |
| `yuvimagewidget.h/.cpp` | 166 | `YuvImageWidget`：`paintEvent()`、`sizeHint()`、像素网格 |
| `rawimagefilename.h/.cpp` | 211 | `metadata()`、`layout()`、键名归一化与显示名 |
| `rawimageframe.h/.cpp` | 101 | `count()`、`validate()`、`render()` |
| `rawimagehistogram.h/.cpp` | 232 | 通道统计与图表绘制 |
| `yuvcontrols.h/.cpp` | 324 | 工具栏控件及其信号、格式匹配高亮 |

三个 `rawimage*` 模块不含任何 widget 代码（直方图用的 `QPainter`
属于 QtGui），各自带独立的 tr 上下文，可以直接在工作线程调用。

顺带的可读性调整：

- `YuvViewer` 的成员从 42 个降到 29 个——`m_hasFileLayout` 加四个
  `m_fileWidth/Height/Stride/Scanline` 合成一个
  `std::optional<RawImageLayout> m_fileLayout`，12 个 widget 指针收成一个
  `QPointer<YuvControls>`；
- 构造函数里 80 行 action 初始化移进 `createActions()`，
  `init()` 里 60 余行工具栏搭建移进 `setupToolBar()`；
- 直方图那个 130 行的单函数拆成 `collectChannels()` + `drawChannel()`，
  图表几何尺寸从散落的局部变量收进 `metrics` 命名空间常量；
- `magic number` 命名化：`pixelGridThreshold`、`zoomInStep`、
  `zoomOutStep`、`zoomRange`、`RawImageFrame::compositePlane`。

### 从 `rawimagedecoder.cpp` 拆出

| 文件 | 行数 | 内容 |
| --- | --- | --- |
| `rawimagedecoder.cpp` | 180 | 基类实现 + `readData()` + 注册表 |
| `rawimagedecoder_p.h` | 253 | 共享原语（布局校验、平面提取、像素描述） |
| `yuvdecoders.cpp` | 1077 | 8 个 YUV 族基类 + 17 个格式 |
| `rgbdecoders.cpp` | 642 | 2 个 RGB 族基类 + 12 个格式 + Y8 |

解码器类原本都在一个匿名命名空间里，注册表直接 `new` 它们。拆开后各文件
保留自己的匿名命名空间，末尾导出一个工厂函数：

```cpp
// rawimagedecoder_p.h
namespace RawImageDecoders {
QList<const RawImageDecoder *> createYuvDecoders();
QList<const RawImageDecoder *> createRgbDecoders();
}

// rawimagedecoder.cpp
const QList<const RawImageDecoder *> &all()
{
    static const QList<const RawImageDecoder *> decoders =
        createYuvDecoders() + createRgbDecoders();
    return decoders;
}
```

用显式工厂而不是静态自注册，是为了保证顺序确定：格式下拉框的顺序就是
这个列表的顺序，跨编译单元的静态初始化顺序是不确定的，靠不住。

原来 `rawimagedecoder_p.h` 里的原语是匿名命名空间里的自由函数，现在改成
`namespace RawImageDecoderHelpers` 下的 `inline` 函数；两个解码器文件
`using namespace RawImageDecoderHelpers;`，函数体一行没动。

## 修掉的风险

### 1. `cleanup()` 之后的悬垂指针

`AbstractViewer::cleanup()` 会 `qDeleteAll(m_toolBars)` 并删掉概览页，
但 `YuvViewer` 只手动置空了 `m_infoTable` 和 `m_histogramLabel`，
剩下 12 个工具栏 widget 指针和 `m_imageWidget` 全部悬垂。

这不只是理论问题——`saveState()` 里那句防御其实拦不住任何东西：

```cpp
if (!m_widthSpinBox || !m_heightSpinBox)   // 指针非空，但已经悬垂
    return {};
```

`retranslate()` 同理，只是碰巧被开头的 `if (toolBars().isEmpty()) return;`
挡住了。目前没崩，纯粹因为 `MainWindow` 的调用顺序恰好都在 `cleanup()`
之前——这是靠调用方的实现细节兜着，不是靠自身保证。

**修复**：`YuvControls` 构造时 `QObject(toolBar)`，成为工具栏的子对象，
工具栏一删它就跟着走；`YuvViewer` 侧四个 UI 成员
（`m_imageWidget`、`m_controls`、`m_infoTable`、`m_histogramLabel`）
一律改 `QPointer`，删除后自动置空，判空检查从此真的有效。
`cleanup()` 里手动置空的代码也就不需要了。

QAction 的所有者是 `YuvViewer` 自己（构造时 `new QAction(this)`），
跨 `cleanup()` 存活并在下次 `init()` 时重新挂到新工具栏上，所以仍是裸指针。

### 2. 帧缓冲滞留

`cleanup()` 没有释放 `m_rawData` 和 `m_image`。viewer 实例被
`ViewerFactory` 长期持有并复用，于是关掉一个 4096×3072 的 Y8 之后，
那十几 MB 会一直留到下次打开文件为止。

**修复**：`cleanup()` 调用新增的 `releaseFrame()`，并重置 `m_image`、
文件名元数据和解析出的布局。

### 3. 打开一个文件读两遍

`setupYuvUi()`（收到 `uiInitialized` 时）会 `reload()` 一次，紧接着
`MainWindow` 调 `restoreState()` 又 `reload()` 一次。第二次
`reload()` 开头的 `cancelAsyncTasks()` 会作废第一次，但第一次已经开始
读盘了——大文件相当于白读一遍。

**修复**：所有调用点改用 `requestReload()`，把同一轮事件循环里的多次
请求合并成一次：

```cpp
void YuvViewer::requestReload()
{
    if (m_reloadPending)
        return;
    m_reloadPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_reloadPending = false;
        if (m_file && m_controls)
            reload();
    }, Qt::QueuedConnection);
}
```

以 `this` 作为 context object，实例在排队期间被销毁时 Qt 会自动取消这次
调用；lambda 里再查一遍 `m_file`/`m_controls`，覆盖 `cleanup()` 已执行
但对象还在的情形。

### 4. 直方图渲染缺自校验

`RawImageHistogram::render()` 直接调 `extractPlane()`，而后者按 layout
索引缓冲区。它的安全性完全依赖调用点——`reload()` 里恰好先调了
`RawImageFrame::render()`（内部会校验）并在失败时提前返回。

**修复**：`render()` 开头自己调一次 `RawImageFrame::validate()`，
不自洽就返回空图。少一张图表无害，越界读不是。

### 5. 其余

| 问题 | 修复 |
| --- | --- |
| `reload()` 校验了 `m_file`/宽高框/`m_decoder`，却直接用 `m_frameSpinBox->value()` | 收进 `YuvControls`，访问器统一校验 |
| 格式匹配高亮挂在 `valueChanged` 上，每次改尺寸都 `QFileInfo::size()` stat 一次文件，且 `expectedByteSize()` 重复算两遍 | 文件大小经 `setFileSize()` 缓存一次，帧大小只算一次 |
| `findByExtension("")` / `findById("")` 会遍历全部 30 个格式才返回空 | 空串直接返回 `nullptr` |
| 纯逻辑自由函数用 `YuvViewer::tr()`，解析层反向依赖 widget 类 | 各模块用 `Q_DECLARE_TR_FUNCTIONS` 声明自己的上下文 |

## 行为兼容性

刻意保持不变的部分：

- **格式列表顺序完全一致**，30 个格式一个不少，`defaultDecoder()`
  仍是 NV12；
- **会话状态二进制格式不变**，字段顺序和向后兼容的 `atEnd()` 逐段读取
  逻辑原样保留，旧状态文件仍可读；
- **扩展名压过会话格式**的优先级保留（这是 `.Y8` 崩溃的修复，见风险 1
  的历史背景）；
- 工具栏布局、快捷键、菜单文案、信息页行序均未改动。

唯一收紧的一处：Zoom In / Zoom Out 的 enable 条件加了"当前有图像"这个
前提。原先只比较缩放系数，无图时 `m_scaleFactor` 和 `m_maxScaleFactor`
都是 1，`1 < 1` 为假所以照样是禁用的——实际表现一致，只是不再依赖这个
巧合。

## 翻译上下文迁移

抽出模块时有 22 条字符串换了 tr 上下文（`YuvViewer` →
`RawImageFileName` / `RawImageFrame` / `RawImageHistogram` / `YuvControls`）。
源串本身一字未改，`lupdate` 的同文本启发式把译文自动带到了新上下文，
按惯例标为 `unfinished` 等人工确认；核对后清除标记。

`lrelease` 结果：114 条，111 finished / 3 unfinished——那 3 条
（`PackedRgb16Decoder` 2 条、`Rgba64Decoder` 1 条）是重构前就存在的。
旧上下文里对应的 22 条转为 `vanished` 保留，`lrelease` 会忽略，留着是给
以后的 `lupdate` 做启发式匹配用。

## 备注

- 验证方式：完整构建无警告，`ReadLints` 无告警，并用合成的 NV12（单帧）
  和 Y8（三帧）文件各跑一遍冒烟测试，确认插件加载、解码、直方图路径正常
  且 stderr 无 `qWarning`。
- 遗留一项未做：直方图仍然每次加载都计算，即使从不打开该页签。它跑在
  工作线程上不卡 UI，但大帧会多花几十到几百毫秒。改成按需计算需要跟踪
  页签可见性，属于行为变更，故未纳入本次纯重构。

# 直方图页

概览标签栏新增 **Histogram**（直方图）页，按分量逐通道显示直方图，
横轴是该分量的**原生取值范围**，附带均值、最小值、最大值统计。

## 显示内容

- 每个分量一行：Y（前景色）、U（蓝）、V（红），或 R / G / B / A / X，
  Bayer 为 R / Gr / Gb / B；
- 横轴是该分量的全部可能取值 `0 .. 2^bits - 1`：8 位格式 0–255，
  P010/I010 0–1023，P016/I016/RGB48 0–65535，RGB565 的 R/B 0–31、G 0–63；
  轴标题写明位深，例如 `Value (10 bit)`；
- 纵轴为该 bin 的采样数（按通道最大值归一化），标出 0、峰值的一半和峰值；
- 标题行显示 `mean / min / max`，均按原生值计算；
- 鼠标悬停在柱上时，提示框给出该 bin 的精确取值区间、采样数和占比，
  例如 `Y = 512 to 515` / `1,024 samples (0.78%)`；
- 色度平面按其实际（子采样）分辨率统计，与文件中的样本一一对应。

### 信息页中的统计值

**Info**（信息）页在文件和布局信息之后，按分量列出同一份统计，每个分量
一组，组标题写明分量名和位深，例如 `Y 分量统计（10 位）`：

| 行 | 含义 |
| --- | --- |
| 采样数 | 该平面的样本个数（色度平面为子采样后的个数） |
| 范围 | 最小值至最大值 |
| 均值 | 算术平均 |
| 中位数 | 排序后居中的样本值；样本数为偶数时取中间两个的平均，所以可能是 `x.5` |
| 标准差 / 方差 | 总体方差（除以采样数，而非采样数减一），因为统计的是平面的全部样本 |

YUV 格式给出 Y / U / V，RGB 格式给出 R / G / B（/ A / X），Bayer 给出
R / Gr / Gb / B，与直方图页的通道一一对应。所有数值都按分量的原生位深
计算，例如 P010 的均值落在 0–1023 之间，不是换算到 8 位后的值。

### 位深从哪来

位深是**分量的属性**，由解码器在平面描述里给出，不是一个全局设置：

- 8 位格式总是 8；
- P010/I010/RGB48/RGBA64/Bayer 等 16 位容器格式取工具栏 **Samples** 下拉框
  的设置（`RawSampleFormat`）。例如把 Bayer 设成 `12 bit LSB`，横轴就是
  0–4095，读取时会屏蔽掉容器里的高 4 位；
- RGB565/555 系列按位域宽度：5 或 6 位。

因此 16 位容器里只有 10 位有效数据时，如果 Samples 仍是满量程 16 位，
直方图会挤在最左边一小段——这正好提示 Samples 设置和数据不匹配。

## 实现要点

### 统计来源

直方图**不从转换后的 RGB 图像**统计（那样 YUV 语义就丢了），也不从
8 位的平面视图统计（那样高位深的精度就丢了），而是直接读原始帧里的样本。

每个解码器用 `planeLayout()` 描述平面 i 在帧里的位置
（`plugins/yuvviewer/rawimagedecoder.h`）：

```cpp
struct RawPlaneLayout {
    qint64 offset;         // 首个样本的字节偏移
    int width, height;     // 平面分辨率（色度平面为子采样后的尺寸）
    qint64 rowStride;      // 行间距（字节）
    int sampleStride;      // 同一行内相邻样本的间距（字节）
    int containerBytes;    // 1，或 2（小端 16 位字）
    int shift, bits;       // 样本 = (container >> shift) & (2^bits - 1)
};
```

`RawImageHistogram::compute()`（`plugins/yuvviewer/rawimagehistogram.cpp`）
先用 `RawImageFrame::validate()` 确认 (decoder, data, layout) 自洽，再对每个
平面用 `fitsIn()` 确认最后一个样本仍在缓冲区内，然后逐行 `readRow()`，按值
计数到 `2^bits` 个 bin 里。平面视图（功能 2）的 `extractPlane()` 读的是同一份
描述，所以两者看到的样本完全一致。

统计值由计数直接推出，不必再遍历一遍帧：均值和方差按取值加权求和（方差用
已知均值做第二遍，避免"平方和减和的平方"在窄分布上的抵消误差），中位数沿
累计计数找到居中的两个样本。最多 65536 个 bin，代价可以忽略。

### 绘制

图表由 `YuvHistogramWidget`（`plugins/yuvviewer/yuvhistogramwidget.cpp`）在
`paintEvent()` 里用 `QPainter` 直接画，不再预先渲染成图片：

- **按宽度重新分组**：统计结果保留全部 `2^bits` 个计数（16 位就是 65536 个），
  绘制时按绘图区的**设备像素**宽度合并成 2 的幂个值一组的 bin，保证每个 bin
  至少一个物理像素宽。页面拉宽、HiDPI 屏幕都会显示更细的直方图，提示框里的
  区间也随之变化；
- **跟随主题**：颜色从控件的 `palette()` 推出。QDarkStyleSheet 只可靠地设置了
  `Window` / `WindowText`，所以坐标轴、绘图区底色都由这两者混合得到，
  亮色、暗色主题下都清晰；
- **布局随字体变化**：左侧留白按刻度标签的最大宽度和字体高度计算，
  字体变大不会裁字；页面过窄时标题行省略、刻度自动减半；
- **切换语言立即生效**：文字在每次绘制时调用 `tr()`，`LanguageChange`
  触发重绘即可，不需要重新统计。

### 线程模型

统计发生在加载的工作线程里，绘制在 UI 线程：

- `RawImageHistogram::compute()` 在 `reload()` 的 worker 中调用，结果
  （`QList<RawImageHistogram::Channel>`）作为 `LoadedFrame::histogram`
  一并返回；
- UI 线程的 done 回调把它交给 `YuvHistogramWidget::setChannels()`。绘制只是
  对最多 65536 个计数做一次分组，代价远小于统计本身；
- 切换平面视图不会重算直方图（直方图描述的是整帧，与视图无关）。

### 页签管理

`AbstractViewer` 新增通用页签助手：

```cpp
void addTabPage(QWidget *page, const QString &title);
```

与 `addInfoTab()` 同样的生命周期：页签由 viewer 拥有，
`cleanup()` 自动删除。直方图页是一个包着 `YuvHistogramWidget` 的
`QScrollArea`（`widgetResizable`）：图表随页面拉伸，窄到最小宽度以下时
出现横向滚动条，通道多于一屏时出现纵向滚动条，概览分栏因此可以任意收窄。

## 备注

- 超大帧的直方图统计约增加几十到几百毫秒加载时间（工作线程，
  不阻塞 UI）。

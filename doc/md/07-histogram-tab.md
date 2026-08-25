# 直方图页

概览标签栏新增 **Histogram**（直方图）页，按分量逐通道显示 256 bin
直方图，附带均值统计。

## 显示内容

- 每个分量一行：Y（灰）、U（蓝）、V（红），或 R / G / B / A / X；
- 横轴 0–255 采样值，纵轴为该 bin 的像素数（按通道最大值归一化）；
- 通道名旁显示均值，例如 `Y（均值 128.4）`；
- 色度平面按其实际（子采样）分辨率统计，与文件中的样本一一对应。

## 实现要点

### 统计来源

直方图**不从转换后的 RGB 图像**统计（那样 YUV 语义就丢了），
而是复用功能 2 的 `extractPlane()`：每个平面提取为 8 位灰度图后
逐像素计数。因此 Y 直方图就是文件中真实的亮度分布。

### 线程模型

统计与渲染都发生在加载的工作线程里：

- `RawImageHistogram::render()`（`plugins/yuvviewer/rawimagehistogram.cpp`）
  在 `reload()` 的 worker 中调用，结果作为 `LoadedFrame::histogram` 一并返回；
- `QPainter` 绘制 `QImage` 在线程间是安全的（只有 `QPixmap` 必须在
  GUI 线程），UI 线程的 done 回调只做一次 `QPixmap::fromImage`；
- 切换平面视图不会重算直方图（直方图描述的是整帧，与视图无关）。

### 页签管理

`AbstractViewer` 新增通用页签助手：

```cpp
void addTabPage(QWidget *page, const QString &title);
```

与 `addInfoTab()` 同样的生命周期：页签由 viewer 拥有，
`cleanup()` 自动删除。直方图页是一个左对齐的 `QLabel`。

## 备注

- 高 bit 深度格式（功能 8）的平面先按高位缩放到 8 位再统计，
  直方图反映的是显示用的 8 位分布；
- 超大帧的直方图统计约增加几十到几百毫秒加载时间（工作线程，
  不阻塞 UI）。

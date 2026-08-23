# 像素探针

鼠标在图像上移动时，状态栏实时显示该像素的**原始采样值**
（未经过 YUV→RGB 转换），例如 `Y=128 U=90 V=200` 或
`R=255 G=128 B=0 A=255`。

## 行为

- 显示格式：`(x, y)  Y=.. U=.. V=..`（坐标 + 原始分量值）；
- 坐标为**合成图像坐标**（即文件中的像素位置）。查看色度平面时，
  平面坐标会按子采样比例换算回合成坐标，因此探针始终报告文件里
  该像素位置的完整 YUV / RGB 值；
- 鼠标移出图像区域后状态栏消息自动清除；
- 加载过程中（上一帧数据已失效）探针自动停用。

## 实现要点

### 解码器接口

`RawImageDecoder` 新增：

```cpp
virtual QString describePixel(const QByteArray &data,
                              const RawImageLayout &layout, int x, int y) const;
```

基类返回空字符串（探针不可用）。各格式家族按布局直接寻址：

| 家族 | 取值方式 |
| --- | --- |
| Planar 4:2:0/4:2:2/4:4:4 | Y 直读；U/V 按平面偏移 + 子采样坐标 |
| Semi-planar (NV12/NV16…) | Y 直读；U/V 在交织平面按 `(x/2)*2+order` |
| Packed 4:2:2 (YUY2…) | 定位宏像素 `(x/2)*4`，按 `componentOffsets()` 取字节 |
| Packed RGB 8-bit | 按 `channelByteOffset()` 取通道字节 |
| RGB565/555 | 读 `quint16`，按 `channelBitLayout()` 解位域，带舍入扩展到 8 位 |

功能 2 引入的 `chromaOrderIsUV()` / `componentOffsets()` /
`channelByteOffset()` / `channelBitLayout()` 在这里全部复用，
单格式解码器无需新增任何代码。

### 查看器

- `YuvViewer::eventFilter()` 拦截图像标签的 `MouseMove` / `Leave`
  事件（`init()` 中 `installEventFilter` + `setMouseTracking`）；
- `compositePosition()` 完成两级坐标换算：
  窗口控件坐标 →（除以缩放比、乘 devicePixelRatio）→ 显示图像坐标
  →（按显示图与布局的尺寸比）→ 合成图像坐标；
- 探针读取的是功能 2 缓存的 `m_rawData`，O(1) 直读，不触发文件 I/O；
- 消息通过 `statusMessage(..., "probe", 0)` 常驻状态栏直到离开。

## 备注

- 16-bit 位打包格式显示的是扩展到 8 位后的值（与画面所见一致）；
- 高 bit 深度格式（P010 等，见 `08-high-bit-depth.md`）会显示
  原始 16 位容器中的采样值。

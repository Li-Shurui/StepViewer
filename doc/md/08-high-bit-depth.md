# 高 bit 深度格式

YUV 查看器新增 6 种每分量超过 8 bit 的格式，与既有的 8-bit 格式共用同一套
布局校验、多帧导航、平面查看、像素探针、直方图和导出功能。

## 新增格式

| 格式 | 布局 | 位深 | 每帧字节数（紧凑） |
| ---- | ---- | ---- | ------------------ |
| P010 | 半平面 4:2:0（Y + 交错 UV） | 10 bit，MSB 对齐于 16 bit 容器 | `width*height*3` |
| P016 | 半平面 4:2:0（Y + 交错 UV） | 16 bit | `width*height*3` |
| I010 | 平面 4:2:0（Y、U、V 三平面） | 10 bit，MSB 对齐于 16 bit 容器 | `width*height*3` |
| I016 | 平面 4:2:0（Y、U、V 三平面） | 16 bit | `width*height*3` |
| RGB48 | 打包 R、G、B 各 16 bit | 16 bit | `width*height*6` |
| RGBA64 | 打包 R、G、B、A 各 16 bit | 16 bit | `width*height*8` |

约定：

- 所有 16 bit 采样均为**小端**存储。
- P010/I010 的有效数据位于 16 bit 容器的高 10 位（MSB 对齐），低 6 位为 0；
  这与 Windows（DXGI `P010`）、GStreamer 等主流实现一致。
- 文件扩展名：`.p010`、`.p016`、`.i010`、`.i016`、`.rgb48`、`.rgba64`
  （大小写不敏感）。
- RGB48 也被写作 **RGB161616**（照 RGB888、RGB565 那样把每通道位宽拼出来），
  布局完全相同，因此 `.rgb161616` 作为 RGB48 的扩展名别名，不单独占一个格式项。

## 实现方式

### 显示转换

OpenCV 的 `cvtColorTwoPlane` 只接受 8 位输入，因此 16 bit YUV 格式先逐采样
取高 8 位（`v >> 8`）降位到紧凑的 8 bit NV12/I420 缓冲，再复用既有的 8 位
转换路径。降位只发生在显示环节，原始数据始终保留在缓存中。

RGB48 通过 OpenCV `COLOR_RGB2RGBA` 转换（16 位深度在转换中保留），输出包装
为 `QImage::Format_RGBA64`；RGBA64 的内存布局与该 QImage 格式完全一致，
直接零转换包装。

### 平面查看与直方图

16 bit 格式的 `planeLayout()` 用 `wordPlane()` 描述平面：2 字节小端容器，
`shift`/`bits` 取自工具栏的样本打包设置（左对齐时 `shift = 16 - bits`）。

- 平面视图（`extractPlane()`）取原生值的高 8 位渲染为灰度图；
- 直方图按原生位深统计：P010/I010 横轴为 0–1023，P016/I016 为 0–65535，
  每个取值一个 bin，见 `07-histogram-tab.md`。

### 像素探针

YUV 格式的探针值按原生位深显示（P010/I010 为 0..1023，P016/I016 为
0..65535），即把 MSB 对齐的容器值右移 `(16 - bitDepth)` 位。RGB48/RGBA64
直接显示 16 bit 原始值（0..65535）。

### 布局校验

- 16 bit 格式的 stride 下限为 `width * 2`（YUV）或 `width * 6 / 8`（RGB），
  在 `validateLayout()` 中单独检查并给出明确错误信息。
- `RawImageDecoder::maximumStride` 从 `maximumDimension * 4` 提升到
  `maximumDimension * 8`，以容纳 RGBA64 每像素 8 字节。

## 精度说明

显示链路（合成图、平面视图、导出 PNG/BMP）均为 8 bit 精度；完整的位深信息
可通过像素探针逐像素读取，直方图也按原生位深统计。这是有意取舍：Qt 的显示
管线最终也要降到 8 bit 输出到屏幕，提前降位可以复用全部既有代码路径，避免
维护一套并行的 16 bit 显示链路；而直方图和探针是用来读数的，必须看到原始值。

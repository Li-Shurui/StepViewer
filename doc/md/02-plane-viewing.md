# 分量平面单独查看

YUV Viewer 可以把图像的单个分量平面（Y / U / V 或 R / G / B / A / X）
以灰度图的形式单独显示，便于检查噪点、色度伪影和通道串扰。

## 用户界面

工具栏在"格式"之后新增 **Plane** 下拉框：

- 第一项 **Composite**（合成图像）是默认的正常渲染结果；
- 其后是当前格式的分量列表，例如 NV12 为 `Y / U / V`，RGBA8888 为
  `R / G / B / A`，RGBX8888 的第四通道如实标记为 `X`；
- 只有一个分量的格式（Y8）下下拉框禁用。

平面以其**原始分辨率**显示：4:2:0 / 4:2:2 格式的色度平面只有
亮度的一半宽（或半宽半高），不做放大，所见即所得。

## 实现要点

### 解码器接口

`RawImageDecoder` 新增两个虚函数（`plugins/yuvviewer/rawimagedecoder.h`）：

```cpp
virtual QStringList planeNames() const { return {}; }
virtual ImageResult extractPlane(const QByteArray &data,
                                 const RawImageLayout &layout, int plane) const;
```

各格式家族统一实现，单格式解码器只需提供顺序/偏移参数：

| 家族 | 提取方式 |
| --- | --- |
| Planar (I420/I422/I444 等) | 直接按平面偏移包装为 `Format_Grayscale8` |
| Semi-planar (NV12/NV16 等) | Y 直接包装；U/V 按 2 字节步长去交织 |
| Packed 4:2:2 (YUY2 等) | 按宏像素内字节偏移步进提取（Y 步长 2，U/V 步长 4） |
| Packed RGB 8-bit | 按通道字节偏移步进提取 |
| RGB565/555 系列 | 按 mask/shift/bits 解位域并线性扩展到 8 位 |

为此给各家族补充了描述布局的虚函数：`chromaOrderIsUV()`（NV12/NV21、
I420/YV12 此前只有 conversionCode）、`componentOffsets()`（packed 422
宏像素内 Y/U/V 字节位置）、`channelByteOffset()` / `channelBitLayout()` /
`fourthChannelName()`（packed RGB）。

### 查看器

- 加载成功后原始帧数据缓存在 `m_rawData`（`QByteArray`，隐式共享），
  切换平面时**不重新读盘**，只在工作线程对缓存数据做提取；
- `reload()` 的工作线程结果改为 `LoadedFrame{ data, image }`，
  一次完成读盘 + 渲染（合成或平面）；
- 平面选择保存在 viewer state 中（追加在帧号之后，旧状态文件兼容）。

## 备注

- 平面提取全部在工作线程执行，大文件切换不会阻塞 UI；
- 16-bit 位打包格式（RGB565 等）的通道按 `(v * 255 + max/2) / max`
  做带舍入的线性扩展，避免简单移位造成的亮度偏差。

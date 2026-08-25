# 多帧导航

YUV Viewer 现在把"文件大小恰好是单帧大小整数倍"的原始数据文件视为多帧序列，
可以在帧之间自由跳转。

## 判定规则

```
frameSize  = decoder->expectedByteSize(layout)   // 单帧字节数
frameCount = fileSize / frameSize                // 必须整除
```

- 文件大小 **不是** 单帧大小的整数倍时，`readData()` 直接报错
  （错误信息会给出帧大小、文件大小和完整布局参数，便于排查格式/尺寸选错的情况）。
- 整除且 `frameCount > 1` 时，帧导航控件自动启用。

## 用户界面

工具栏在"格式"下拉框之后新增：

| 控件 | 说明 |
| --- | --- |
| `Frame:` 数字框 | 1 起始的帧号，超出范围时自动钳制到最大帧 |
| `/ N` 标签 | 文件中的总帧数 |
| 上一帧 / 下一帧按钮 | 快捷键 `PgUp` / `PgDn` |

切换帧号会触发一次普通的 `reload()`：取消挂起的加载任务，
按 `frameIndex * frameSize` 偏移 `seek()` 后只读取目标帧的数据，
读取与转换仍在工作线程完成并带进度上报。

## 实现要点

- `RawImageDecoder::readData()` 新增 `frameIndex` 参数
  （`plugins/yuvviewer/rawimagedecoder.cpp`），原有的两个重载保留，
  默认读第 0 帧，其它调用方不受影响。
- 每次 `reload()` 前由 `RawImageFrame::count()` 根据当前布局和文件大小
  重新计算帧数，再交给 `YuvControls::setFrameCount()` 更新数字框范围与
  启用状态（用 `QSignalBlocker` 避免递归触发）。
- 信息页新增"帧大小 / 文件大小 / 帧数"三行
  （此前"文件大小"实际显示的是单帧大小，已修正）。
- `saveState()` / `restoreState()` 追加保存帧号；旧版本保存的状态没有该字段，
  读取时按第 1 帧处理，保持向后兼容。
